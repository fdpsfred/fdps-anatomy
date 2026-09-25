/* title.c -- the front-of-house screens: the title screen itself, its
 * attract-mode demo, the game-over screen, and the full-screen FMV playback
 * they reach for.  The ending is ending.c's.
 *
 * See title.h for what each entry point promises its caller.  This file holds
 * no state of its own: everything it touches belongs to another module or to
 * the machine.
 *
 * sprintf, fopen, fread and fclose come from <stdio.h>, memset and memmove
 * from <string.h>, kbhit, getch and inp from <conio.h>, malloc and free from
 * <stdlib.h> and spawnlp from <process.h>, and every one of them is a real
 * library call in the image rather than an inline expansion -- CALL 00042d41,
 * CALL 00042cd0,
 * CALL 00043570, CALL 00043587, CALL 000435a2, CALL 0003d375, CALL 0003d478,
 * CALL 0003d4e4 and CALL 0003d514 -- because the flag that would inline the
 * string and character routines, -oi, is not in this build's set
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
#include "chapter.h"
#include "keybd.h"
#include "mapai.h"
#include "palette.h"
#include "roster.h"
#include "saf.h"
#include "save.h"
#include "savefile.h"
#include "sprite.h"
#include "unit.h"
#include "vfs.h"
#include "village.h"
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

/* The chapter the demo battle is fought on, raw as
   data_fdps_chapter_current_chapter_id counts them -- MOV dword ptr
   [0x00069cf4],0x19 at 0002ac1c.  The player is told chapters from one, so
   this is the twenty-sixth. */
#define DEMO_CHAPTER_ID 0x19

/* The twelve characters the demo enrols, in the order the twelve
   PUSH/CALL pairs at 0002ac37..0002aca7 enrol them.  They are portrait ids
   (assets/characters.md): the eleven playable characters other than 琴琴,
   and 12, which is not a party member at all but 索爾, the guest hero of
   chapters 1 to 6 and 26, enrolled from his own FRIAPRDA.DAT row.  Several
   later indices hold byte copies of that row, but row 12 is 索爾's: his
   guide-listed stats come out of it.  The order is what decides which map
   slot each of them ends up standing in, because fdps_build_map_unit_array
   fills player slot i from roster slot i. */
#define DEMO_CHAR_RANDIS 0
#define DEMO_CHAR_FLARENA 1
#define DEMO_CHAR_BRANDO 8
#define DEMO_CHAR_GAIA 9
#define DEMO_CHAR_FEITAGA 2
#define DEMO_CHAR_SAN 10
#define DEMO_CHAR_JUNA 3
#define DEMO_CHAR_LANCELOT 11
#define DEMO_CHAR_ARC 4
#define DEMO_CHAR_SOL 12
#define DEMO_CHAR_MARIANNE 5
#define DEMO_CHAR_JULIAN 6

/* The cursor overlay is switched off for the whole demo: nobody is choosing
   anything, so there is no cursor to show.  MOV dword ptr [0x00069cd0],0x0 at
   0002acb4, after the chapter reset has just put the same global at 1. */
#define DEMO_CURSOR_DRAW_MODE_OFF 0

/* One unit is moved before the battle starts.  MAP25.COD stands player slot 3
   on its own eleven tiles to the right of the other eleven, and this store
   brings it back to the left edge of the row -- MOV byte ptr [EAX],0x4 at
   0002acce, on the record fdps_get_unit_record(3) returned.  Only the x
   coordinate is written; the tile row is the map's. */
#define DEMO_MOVED_UNIT 3
#define DEMO_MOVED_UNIT_POS_X 4

/* The behaviour reset covers unit slots 0 through 11 inclusive -- PUSH 0x0 /
   PUSH 0xb / PUSH 0x0 at 0002acd1..0002acd5, whose arguments are the first
   index, the last index and the behaviour to OR in.  Behaviour 0 is the plain
   fighter, which is what makes the demo a battle rather than a tableau. */
#define DEMO_PARTY_LAST_UNIT 0xb
#define DEMO_BEHAVIOR_PLAIN_FIGHTER 0

/* The two spells the demo hands out so that the showcase party has something
   to cast: 萬神降臨 to the unit in slot 1 and 封神裂震 to the one in slot 4
   (assets/spells.md).  PUSH 0x27 / PUSH 0x1 at 0002acdf and PUSH 0xb /
   PUSH 0x4 at 0002aceb -- the unit index is pushed last and so is the first
   argument. */
#define DEMO_GRANT_UNIT_A 1
#define DEMO_GRANT_SPELL_A 0x27
#define DEMO_GRANT_UNIT_B 4
#define DEMO_GRANT_SPELL_B 0x0b

/* The canned stats, and how many units get them.  The bound is the twelve
   player slots MAP25.DAT opens, not data_fdps_map_unit_count -- CMP dword ptr
   [EBP-0xc],0xc at 0002acfe -- so the scripted deployments behind them keep
   the numbers their own records gave them. */
#define DEMO_PARTY_UNITS 0xc

/* The side byte the twelve are given -- MOV byte ptr [EAX+0x6],0x1 at
   0002ad20.  Side 1 is the guest/NPC side, not the player's own, which is 2
   (PLAYER_SIDE in src/deploy.c, from CMP EAX,0x2 / JNZ at 000237cf; see also
   the side map in aitarget.h).  fdps_build_map_unit_array has just stood
   every party record on side 2, so this store is what takes the showcase
   party off player control and hands it to the map AI.  It is also the value
   the actor loop forwards as side_select -- MOV AL,byte ptr [EAX+0x6] / AND
   EAX,0xff / PUSH EAX at 0002adb0..0002adb8 -- so the twelve act on the NPC
   phase, whose target filter keeps the side-0 deployments (mapai.h). */
#define DEMO_SIDE_NPC 1

#define DEMO_HP 2000
#define DEMO_MP 800
#define DEMO_ATTACK 800
#define DEMO_HIT 400

/* How many passes over the unit array the demo makes before it gives up and
   returns on its own -- MOV dword ptr [EBP-0x8],0x2 at 0002ad5c. */
#define DEMO_PASSES 2

/* The turn reset is run once more in the middle of every pass, on the unit
   index just past the twelve player slots -- CMP dword ptr [EBP-0xc],0xc /
   JNZ at 0002adc5. */
#define DEMO_MID_PASS_RESET_UNIT 0xc

/* What counts as "the player pressed something".  fdps_read_keyboard_queue
   answers with a make code in 0x01..0x7f or with 0xff for an empty ring
   (keybd.h), and this test takes anything BELOW 0x7f as a key -- CMP EAX,0x7f
   / JGE at 0002adda.  A make code of exactly 0x7f therefore falls on the
   no-key side along with the marker. */
#define DEMO_KEY_PRESSED_BELOW 0x7f

/* 0002ac10.  Straight-line staging, then a two-pass loop with an inner walk
   over the unit array; see title.h for what the demo is and what it leaves
   behind.

   THE RECORD POINTER IS RE-RESOLVED AND THEN HELD ACROSS ONE CALL.  Inside the
   inner loop fdps_get_unit_record runs before fdps_unit_is_retired and the
   pointer it returned is what the side byte is read through afterwards
   (MOV EAX,[EBP-0x4] / MOV AL,[EAX+0x6] at 0002adad), so the order of those
   two calls is part of the function: resolving the record after the retirement
   test would be a different program if anything under it moved the array.

   THE SIDE BYTE IS WIDENED, NOT SIGN-EXTENDED.  AND EAX,0xff at 0002adb3
   follows the byte load, so a side of 0x80 or above reaches the behaviour step
   as a number above 127 rather than as a negative one.

   The key check ends the demo by setting the pass counter to 1 and leaving the
   inner loop, not by returning: the turn reset at the end of the pass still
   runs, and it is the decrement after it that takes the counter to 0. */
void fdps_title_demo(void)
{
    struct fdps_unit_record *unit_record;
    int unit_index;
    int passes_left;
    int key_code;

    data_fdps_chapter_current_chapter_id = DEMO_CHAPTER_ID;
    data_fdps_ui_play_active_flag = 0;
    data_fdps_roster_member_count = 0;

    fdps_roster_add_character(DEMO_CHAR_RANDIS);
    fdps_roster_add_character(DEMO_CHAR_FLARENA);
    fdps_roster_add_character(DEMO_CHAR_BRANDO);
    fdps_roster_add_character(DEMO_CHAR_GAIA);
    fdps_roster_add_character(DEMO_CHAR_FEITAGA);
    fdps_roster_add_character(DEMO_CHAR_SAN);
    fdps_roster_add_character(DEMO_CHAR_JUNA);
    fdps_roster_add_character(DEMO_CHAR_LANCELOT);
    fdps_roster_add_character(DEMO_CHAR_ARC);
    fdps_roster_add_character(DEMO_CHAR_SOL);
    fdps_roster_add_character(DEMO_CHAR_MARIANNE);
    fdps_roster_add_character(DEMO_CHAR_JULIAN);

    fdps_chapter_state_reset();
    data_fdps_map_cursor_draw_mode = DEMO_CURSOR_DRAW_MODE_OFF;

    unit_record = fdps_get_unit_record(DEMO_MOVED_UNIT);
    unit_record->pos_x = DEMO_MOVED_UNIT_POS_X;

    fdps_object_set_field34_low_nibble_range(0, DEMO_PARTY_LAST_UNIT,
                                            DEMO_BEHAVIOR_PLAIN_FIGHTER);
    fdps_set_flag_bit(DEMO_GRANT_UNIT_A, DEMO_GRANT_SPELL_A);
    fdps_set_flag_bit(DEMO_GRANT_UNIT_B, DEMO_GRANT_SPELL_B);

    for (unit_index = 0; unit_index < DEMO_PARTY_UNITS; unit_index++) {
        unit_record = fdps_get_unit_record(unit_index);
        unit_record->side = DEMO_SIDE_NPC;
        unit_record->hp_current = DEMO_HP;
        unit_record->hp_max = DEMO_HP;
        unit_record->mp_current = DEMO_MP;
        unit_record->mp_max = DEMO_MP;
        unit_record->ap = DEMO_ATTACK;
        unit_record->hit = DEMO_HIT;
    }

    passes_left = DEMO_PASSES;
    fdps_flush_keyboard_queue();

    while (passes_left != 0) {
        for (unit_index = 0;
             unit_index < data_fdps_map_unit_count;
             unit_index++) {
            unit_record = fdps_get_unit_record(unit_index);
            if (fdps_unit_is_retired(unit_index) == 0) {
                fdps_map_actor_behavior_step(unit_index,
                                             (int) unit_record->side);
            }
            if (unit_index == DEMO_MID_PASS_RESET_UNIT) {
                fdps_units_clear_status_bit7();
            }
            key_code = fdps_read_keyboard_queue();
            if (key_code < DEMO_KEY_PRESSED_BELOW) {
                passes_left = 1;
                break;
            }
        }
        fdps_units_clear_status_bit7();
        passes_left--;
    }

    free(data_fdps_roster_array_ptr);
    data_fdps_roster_member_count = 0;
    data_fdps_ui_play_active_flag = 1;
    memset((void *) VGA_SCREEN_BASE, 0, (size_t) VGA_SCREEN_BYTES);
    fdps_flush_keyboard_queue();
}

/* MISC.VFS and the five members the attract cycle pulls out of it, all of them
   literals in the original's writable data segment: "MISC.VFS" at 0x60128 and
   the member names at 0x61e24, 0x61e30, 0x61e44, 0x61e50 and 0x61e5c.  The
   container's lookup upper-cases the query in place before it compares
   (resource_info/vfs.md), so the mixed case here reaches the upper-case entry
   names the containers actually hold. */
#define TITLE_ARCHIVE "MISC.VFS"
#define TITLE_LOGO_PALETTE "Dynasty.pal"
#define TITLE_LOGO_CLIP "Logo.saf"
#define TITLE_ENTRY_CEL "StSel.cel"
#define TITLE_BOARD_CEL "StBoard.cel"
#define TITLE_BOARD_PALETTE "StBoard.pal"

/* The 368x248 page the logo clip is composed on and the 320x200 window inside
   it that reaches the adapter: PUSH 0x16480 at 0002a3d5, the four request
   slots written at 0002a3e8..0002a400, and ADD EAX,0x2298 at 0002a500.  It is
   the same page geometry saf.h's player and the game-over screen use, and the
   window offset is again 0x18 * 0x170 + 0x18, so a frame layer carrying screen
   coordinates lands on those screen coordinates. */
#define TITLE_PAGE_PITCH 0x170
#define TITLE_PAGE_ROWS 0xf8
#define TITLE_PAGE_BYTES 0x16480
#define TITLE_PAGE_MARGIN 0x18
#define TITLE_PAGE_WINDOW_AT 0x2298

/* FDE.SAV as the title screen reads it, which is the same file and the same
   two numbers savefile.c uses: PUSH 0x59cb at 0002a317, 0002a32b, 0002a34a and
   0002a366, CMP EAX,[EDX + 0x59c7] at 0002a37a, ADD EAX,0x30c3 at 0002a35e and
   the byte at +0x2 of that header tested against 0xff at 0002a38d.  +0x2 is
   the resume image's chapter byte (savefile.h), and 0xff is what the save side
   leaves there when no battle is in progress, so the two save-dependent menu
   entries are enabled by a file that verifies AND holds a battle. */
#define TITLE_SAVE_FILE "FDE.SAV"
#define TITLE_SAVE_READ_MODE "rb"
#define TITLE_SAVE_IMAGE_BYTES 0x59cb
#define TITLE_SAVE_CHECKSUM_AT 0x59c7
#define TITLE_SAVE_RESUME_HEADER_AT 0x30c3
#define TITLE_RESUME_CHAPTER 0x02
#define TITLE_RESUME_NO_BATTLE 0xff

/* The opening movie.  The stem is formatted with the alternating index plus
   one, so the two passes of the attract cycle play "FD1" and "FD2"; the format
   string is the literal at 0x61e3c and the stack buffer it is formatted into
   is the twelve bytes at [EBP-0x58].  The music index handed to
   fdps_cd_set_music_track behind it is PUSH 0x1 at 0002a572, which is CD track
   2 (cdaudio.h). */
#define TITLE_MOVIE_STEM_FORMAT "FD%d"
#define TITLE_MOVIE_STEM_MAX 12
#define TITLE_MOVIE_COUNT 2
#define TITLE_MUSIC_INDEX 1

/* CMP EAX,0x80 / JGE at 0002a4b1, on the scancode ring's answer widened with
   AND EAX,0xff.  Anything below 0x80 is a make code and ends the logo clip;
   the 0xff the ring answers with when it is empty is not.  The demo's own test
   above uses 0x7f for the same purpose and this one uses 0x80 -- the two
   really do differ in the original, and 0x7f itself therefore counts as a key
   here and not there. */
#define TITLE_ANIM_KEY_PRESSED_BELOW 0x80

/* The four keys the menu reads: Enter and Space accept, Up and Down move.
   CMP against 0x1c, 0x39, 0x48 and 0x50 at 0002a616, 0002a61c, 0002a634 and
   0002a651. */
#define TITLE_KEY_ENTER 0x1c
#define TITLE_KEY_SPACE 0x39
#define TITLE_KEY_UP 0x48
#define TITLE_KEY_DOWN 0x50

/* The four menu entries and the three cel frames each of them has.  The
   selection moves modulo four with a signed remainder -- MOV EBX,0x4 / SAR
   EDX,0x1f / IDIV EBX at 0002a640 and 0002a65b -- and the frame drawn for
   entry i is i * 3 plus its state, LEA EAX,[EAX + EAX*0x2] / ADD
   EAX,[EBP-0x3c] at 0002a6ea.  State 0 is a plain entry, 1 is the highlighted
   one and 2 is greyed out; the flag array starts {0, 2, 2, 0} -- the dword at
   0x29210 moved into [EBP-0x1c] at 0002a2ca -- so the two save-dependent
   entries are greyed until FDE.SAV says otherwise.  Every entry's cel goes at
   the same column and one 0x11-pixel row further down, PUSH 0x77 at 0002a6dc
   and IMUL EAX,[EBP-0x48],0x11 / ADD EAX,0x48 at 0002a6d4. */
#define TITLE_ENTRY_COUNT 4
#define TITLE_ENTRY_NEW_GAME 0
#define TITLE_ENTRY_LOAD_GAME 1
#define TITLE_ENTRY_CONTINUE 2
#define TITLE_ENTRY_STATE_ENABLED 0
#define TITLE_ENTRY_STATE_SELECTED 1
#define TITLE_ENTRY_STATE_DISABLED 2
#define TITLE_ENTRY_FRAMES_PER_ENTRY 3
#define TITLE_ENTRY_X 0x77
#define TITLE_ENTRY_FIRST_Y 0x48
#define TITLE_ENTRY_Y_PITCH 0x11

/* The idle counter and the fade.  MOV [EBP-0x38],0x6d6 at 0002a5f8 is one tick
   per menu frame, so the menu gives the player 1750 ticks before it hands the
   screen to the demo.  The fade is ten steps, CMP [EBP-0x48],0xa / JL at
   0002a764, each biasing all three channels by minus six times the step, NEG
   EAX / IMUL EAX,EAX,0x6 at 0002a788.  The last step is therefore -54 and not
   -63: the fade stops short of black and the frame clear below it is what
   finishes the job. */
#define TITLE_IDLE_TICKS 0x6d6
#define TITLE_FADE_STEPS 10
#define TITLE_FADE_STEP_BIAS 6

/* The new-game branch: chapter id 0, and the cursor draw mode every branch
   publishes on its way out.  MOV [0x00069cf4],0x0 at 0002a841 and MOV
   [0x00069cd0],0x1 at 0002a8fe. */
#define TITLE_NEW_GAME_CHAPTER 0
#define TITLE_CURSOR_DRAW_MODE_ON 1

/* What the load screen answers with: 1 for a load that happened and -1 for a
   screen the player backed out of (save.h).  CMP [EBP-0x40],-0x1 at 0002a8af
   folds the -1 into 0 so the title menu comes back up, and CMP [EBP-0x40],0x1
   at 0002a8cf is what sends a load that happened into the village. */
#define TITLE_LOAD_CANCELLED (-1)
#define TITLE_LOAD_ACCEPTED 1

/* 0002a2b0.  The whole front of house: the save-dependent menu gate, then an
   attract cycle of logo clip, opening movie and title menu that repeats until
   the player picks something.  See title.h for what it promises main.

   VALUES USED AFTER A CALL, and what the assembly says each one is.  fopen's
   EAX at 0002a30a is tested against 0 at 0002a30d and is the whole of the save
   gate's guard.  Both mallocs' and all five fdps_vfs_load_entry's EAX go
   straight into the slot that owns the block and are re-read from there;
   nothing tests any of them, so an allocation or a member lookup that failed
   is not noticed here.  fdps_compute_save_checksum's EAX at 0002a374 is
   compared against the dword at the image's +0x59c7 at 0002a37a and against
   nothing else.  fdps_saf_advance_tick's EAX at 0002a4a4 is stored as the
   clip's own done flag, and the reset call above it at 0002a443 leaves its
   answer discarded.  fdps_read_keyboard_queue's EAX is widened with AND
   EAX,0xff at both sites and then compared -- against 0x80 in the clip,
   against the four key codes in the menu.  inp's AL is TEST AL,0x8 every time.
   And fdps_load_game_screen's EAX at 0002a8ac becomes the menu loop's own done
   flag, which is why a cancelled screen brings the menu back.
   fdps_run_village_phase returns an int and it is discarded -- no store and no
   test follows CALL 0x00031210 at 0002a8d5.

   CONTROL FLOW.  An outer loop over the attract cycle, an inner loop over the
   title menu that repeats the whole menu presentation until a selection has
   been acted on, and inside that the per-frame loop; the clip has a frame loop
   of its own before the movie.  Every one of them is top-tested.  The
   selection index and the flag array live OUTSIDE all of them, so a demo that
   runs and a movie that plays leave the player on the entry they were on.

   THE MENU IS RE-PRESENTED, NOT RESUMED.  The three cels, the palette and the
   page are loaded at the top of the inner loop and freed at its foot, so a
   cancelled load screen goes all the way round and loads them again.

   THE TICK LATCH IS DELIBERATELY NOT INITIALISED, exactly as in
   fdps_show_game_over: [EBP-0x44] is read at 0002a50e before anything has
   written it (rebuild_info/pitfalls.md).

   THE CHAPTER HANDLER IS REACHED THROUGH THE GLOBAL, not through the 0 that
   was just stored in it.  MOV EAX,[0x00069cf4] at 0002a852 re-reads
   data_fdps_chapter_current_chapter_id after the store two instructions above,
   and that re-read is the table index (chapter.h).

   THE FADE WAITS ONLY FOR THE RETRACE TO BEGIN.  The clip and the menu frame
   both wait for it to begin and then to end before they present, because both
   of them copy a page to the adapter; the fade writes nothing but the DAC, so
   the second spin is not there (0002a774 has no counterpart).

   WHAT A RUN LEAVES BEHIND.  Every branch publishes
   data_fdps_map_cursor_draw_mode, the terrain panel flag is put back up by the
   new-game branch alone, and data_fdps_village_skip_save_prompt_flag is set by
   the load branch alone and left standing.  The frame is blank and the DAC
   holds the unbiased master palette whichever way the menu ended. */
int fdps_title_screen(void)
{
    /* The nine-dword draw request sprite.h describes.  It owns both of the
       clip's blocks: element 0 is the composition page and element 5 the .SAF
       itself, and the original keeps neither anywhere else -- both frees read
       the slot back. */
    int request[DRAW_REQUEST_DWORDS];
    /* The three-dword playback cursor saf.h describes. */
    int playback_cursor[SAF_CURSOR_DWORDS];
    /* One byte per menu entry: 0 selectable, 2 greyed out.  Read back
       zero-extended, XOR EAX,EAX / MOV AL at 0002a6c4. */
    unsigned char menu_entry_flags[TITLE_ENTRY_COUNT] = {
        TITLE_ENTRY_STATE_ENABLED,
        TITLE_ENTRY_STATE_DISABLED,
        TITLE_ENTRY_STATE_DISABLED,
        TITLE_ENTRY_STATE_ENABLED
    };
    /* "FD1" or "FD2", formatted per pass.  The original's slot is twelve
       bytes; nothing bounds what is written into it. */
    char movie_stem[TITLE_MOVIE_STEM_MAX];
    /* FDE.SAV read whole and decrypted in place, and the scalar header inside
       it.  Both are the gate's and are gone before the attract cycle starts. */
    unsigned char *save_image;
    unsigned char *resume_header;
    /* StSel.cel, StBoard.cel and the 320x200 page the menu frame is composed
       on.  All three are taken and released once per menu presentation. */
    unsigned char *entry_cel;
    unsigned char *board_cel;
    unsigned char *menu_page;
    /* The page the load screen is handed to restore behind itself.  The
       original reuses one stack slot, [EBP-0x34], for this and for the two
       palettes below it. */
    unsigned char *load_screen_page;
    /* Dynasty.pal for the clip and StBoard.pal for the menu, in turn. */
    struct fdps_palette_entry *title_palette;
    FILE *save_fp;
    /* The tick the previous frame ended on.  Deliberately not initialised --
       see the note above. */
    unsigned int last_tick;
    /* Which menu entry the cursor is on, 0..3.  It outlives every loop. */
    int menu_selection;
    /* Which opening movie the next pass plays, 0 or 1. */
    int movie_index;
    /* The outer loop's flag: set once a selection has been acted on and the
       title screen is over. */
    int title_over;
    /* The clip frame loop's flag: fdps_saf_advance_tick's answer, forced to 1
       by a keypress. */
    int clip_over;
    /* The menu loop's flag: set by the dispatch below, or taken from the load
       screen's own answer. */
    int selection_done;
    /* How many frames the current menu presentation has left before it hands
       the screen to the demo. */
    int idle_ticks_left;
    /* The make code this menu frame read, widened unsigned. */
    int scancode;
    /* Which of an entry's three cel frames this frame draws. */
    int entry_state;
    /* The entry the draw loop is on, and the step the fade is on. */
    int entry_index;
    int fade_step;
    /* The menu frame loop's flag, a byte in the original's frame. */
    char menu_over;

    menu_selection = TITLE_ENTRY_NEW_GAME;
    selection_done = 0;
    title_over = 0;
    movie_index = 0;

    fdps_flush_keyboard_queue();
    data_fdps_ui_play_active_flag = 1;
    data_fdps_bonus_lottery_drawn_flag = 0;

    /* The gate.  A file that is not there leaves both save-dependent entries
       greyed; a file that is there is read whole, decrypted and verified, and
       the two entries are enabled only when the checksum holds AND the resume
       image names a chapter.  Nothing checks the allocation and nothing checks
       how much fread actually read. */
    save_fp = fopen(TITLE_SAVE_FILE, TITLE_SAVE_READ_MODE);
    if (save_fp != NULL) {
        save_image = (unsigned char *) malloc((size_t) TITLE_SAVE_IMAGE_BYTES);
        fread(save_image, 1, (size_t) TITLE_SAVE_IMAGE_BYTES, save_fp);
        fclose(save_fp);
        fdps_xor_crypt_buffer(save_image,
                              (unsigned int) TITLE_SAVE_IMAGE_BYTES);
        resume_header = save_image + TITLE_SAVE_RESUME_HEADER_AT;
        if (fdps_compute_save_checksum(save_image,
                                       (unsigned int) TITLE_SAVE_IMAGE_BYTES)
                == *(unsigned int *) (save_image + TITLE_SAVE_CHECKSUM_AT)
            && resume_header[TITLE_RESUME_CHAPTER] != TITLE_RESUME_NO_BATTLE) {
            menu_entry_flags[TITLE_ENTRY_LOAD_GAME] = TITLE_ENTRY_STATE_ENABLED;
            menu_entry_flags[TITLE_ENTRY_CONTINUE] = TITLE_ENTRY_STATE_ENABLED;
        }
        free(save_image);
    }

    while (title_over == 0) {
        fdps_cd_stop_audio();
        clip_over = 0;

        title_palette = (struct fdps_palette_entry *)
            fdps_vfs_load_entry(TITLE_ARCHIVE, TITLE_LOGO_PALETTE);

        request[DRAW_REQUEST_DEST_BASE] =
            (int) malloc((size_t) TITLE_PAGE_BYTES);
        request[DRAW_REQUEST_DEST_PITCH] = TITLE_PAGE_PITCH;
        request[DRAW_REQUEST_DEST_ROWS] = TITLE_PAGE_ROWS;
        request[DRAW_REQUEST_X] = TITLE_PAGE_MARGIN;
        request[DRAW_REQUEST_Y] = TITLE_PAGE_MARGIN;
        request[DRAW_REQUEST_IMAGE] =
            (int) fdps_vfs_load_entry(TITLE_ARCHIVE, TITLE_LOGO_CLIP);
        request[DRAW_REQUEST_ITEM_INDEX] = 0;
        request[DRAW_REQUEST_BLIT_OPERAND] = 0;
        request[DRAW_REQUEST_BLIT_MODE] = BLIT_MODE_OPAQUE;

        playback_cursor[SAF_CURSOR_IMAGE] = request[DRAW_REQUEST_IMAGE];
        fdps_saf_advance_tick(playback_cursor, 1);

        fdps_set_palette_range(title_palette, VGA_DAC_FIRST_ENTRY,
                               VGA_DAC_LAST_ENTRY, 0, 0, 0);
        free(title_palette);

        while (clip_over == 0) {
            request[DRAW_REQUEST_ITEM_INDEX] =
                playback_cursor[SAF_CURSOR_FRAME_INDEX];
            fdps_draw_composite_sprite(request, 1);
            clip_over = fdps_saf_advance_tick(playback_cursor, 0);
            if (fdps_read_keyboard_queue() < TITLE_ANIM_KEY_PRESSED_BELOW) {
                clip_over = 1;
            }

            while ((inp(VGA_INPUT_STATUS_1)
                    & VGA_STATUS_VERTICAL_RETRACE) == 0) {
                /* Spin until the retrace begins. */
            }
            while ((inp(VGA_INPUT_STATUS_1)
                    & VGA_STATUS_VERTICAL_RETRACE) != 0) {
                /* And until it ends, so the present runs inside the displayed
                   part of the frame. */
            }
            fdps_blit_rect((unsigned int) request[DRAW_REQUEST_DEST_BASE]
                               + TITLE_PAGE_WINDOW_AT,
                           TITLE_PAGE_PITCH, (void *) VGA_SCREEN_BASE,
                           VGA_SCREEN_PITCH, VGA_SCREEN_PITCH,
                           VGA_SCREEN_ROWS);

            while (last_tick == data_fdps_timer_tick_counter) {
                /* spin: only the timer interrupt can end this */
            }
            last_tick = data_fdps_timer_tick_counter;
        }

        free((void *) request[DRAW_REQUEST_DEST_BASE]);
        free((void *) request[DRAW_REQUEST_IMAGE]);

        selection_done = 0;
        sprintf(movie_stem, TITLE_MOVIE_STEM_FORMAT, movie_index + 1);
        fdps_play_movie(movie_stem);
        data_fdps_audio_bgm_enabled_flag = 1;
        fdps_cd_set_music_track(TITLE_MUSIC_INDEX);

        while (selection_done == 0) {
            entry_cel = (unsigned char *)
                fdps_vfs_load_entry(TITLE_ARCHIVE, TITLE_ENTRY_CEL);
            board_cel = (unsigned char *)
                fdps_vfs_load_entry(TITLE_ARCHIVE, TITLE_BOARD_CEL);
            title_palette = (struct fdps_palette_entry *)
                fdps_vfs_load_entry(TITLE_ARCHIVE, TITLE_BOARD_PALETTE);
            fdps_set_palette_range(title_palette, VGA_DAC_FIRST_ENTRY,
                                   VGA_DAC_LAST_ENTRY, 0, 0, 0);
            menu_page = (unsigned char *) malloc((size_t) VGA_SCREEN_BYTES);
            menu_over = 0;
            idle_ticks_left = TITLE_IDLE_TICKS;

            while (menu_over == 0) {
                scancode = (int) fdps_read_keyboard_queue();
                if ((scancode == TITLE_KEY_ENTER
                     || scancode == TITLE_KEY_SPACE)
                    && menu_entry_flags[menu_selection]
                           == TITLE_ENTRY_STATE_ENABLED) {
                    menu_over = 1;
                } else if (scancode == TITLE_KEY_UP) {
                    menu_selection = (menu_selection + TITLE_ENTRY_COUNT - 1)
                                     % TITLE_ENTRY_COUNT;
                } else if (scancode == TITLE_KEY_DOWN) {
                    menu_selection = (menu_selection + 1) % TITLE_ENTRY_COUNT;
                }

                memset(menu_page, 0, (size_t) VGA_SCREEN_BYTES);
                fdps_cel_blit_sprite(board_cel, 0, menu_page,
                                     VGA_SCREEN_PITCH, 0, 0, 0,
                                     BLIT_MODE_OPAQUE);

                for (entry_index = 0; entry_index < TITLE_ENTRY_COUNT;
                     entry_index++) {
                    if (menu_selection == entry_index) {
                        entry_state = TITLE_ENTRY_STATE_SELECTED;
                    } else {
                        entry_state = (int) menu_entry_flags[entry_index];
                    }
                    fdps_cel_blit_sprite(entry_cel,
                                         entry_index
                                             * TITLE_ENTRY_FRAMES_PER_ENTRY
                                             + entry_state,
                                         menu_page, VGA_SCREEN_PITCH,
                                         TITLE_ENTRY_X,
                                         entry_index * TITLE_ENTRY_Y_PITCH
                                             + TITLE_ENTRY_FIRST_Y,
                                         0, BLIT_MODE_OPAQUE);
                }

                while ((inp(VGA_INPUT_STATUS_1)
                        & VGA_STATUS_VERTICAL_RETRACE) == 0) {
                    /* The same pair of spins, for the same reason. */
                }
                while ((inp(VGA_INPUT_STATUS_1)
                        & VGA_STATUS_VERTICAL_RETRACE) != 0) {
                }
                memmove((void *) VGA_SCREEN_BASE, menu_page,
                        (size_t) VGA_SCREEN_BYTES);

                while (last_tick == data_fdps_timer_tick_counter) {
                    /* spin: only the timer interrupt can end this */
                }
                last_tick = data_fdps_timer_tick_counter;

                idle_ticks_left--;
                if (idle_ticks_left == 0) {
                    menu_over = 1;
                }
            }

            for (fade_step = 0; fade_step < TITLE_FADE_STEPS; fade_step++) {
                while ((inp(VGA_INPUT_STATUS_1)
                        & VGA_STATUS_VERTICAL_RETRACE) == 0) {
                    /* Only the beginning of the retrace is waited for here:
                       nothing is copied to the adapter, so there is no present
                       that has to fall inside the displayed part. */
                }
                fdps_set_palette_range(title_palette, VGA_DAC_FIRST_ENTRY,
                                       VGA_DAC_LAST_ENTRY,
                                       -fade_step * TITLE_FADE_STEP_BIAS,
                                       -fade_step * TITLE_FADE_STEP_BIAS,
                                       -fade_step * TITLE_FADE_STEP_BIAS);
                while (last_tick == data_fdps_timer_tick_counter) {
                    /* spin: only the timer interrupt can end this */
                }
                last_tick = data_fdps_timer_tick_counter;
            }

            memset((void *) VGA_SCREEN_BASE, 0, (size_t) VGA_SCREEN_BYTES);
            free(entry_cel);
            free(board_cel);
            free(menu_page);
            free(title_palette);
            fdps_set_palette_range(
                (struct fdps_palette_entry *) data_fdps_vga_main_palette_ptr,
                VGA_DAC_FIRST_ENTRY, VGA_DAC_LAST_ENTRY, 0, 0, 0);

            if (idle_ticks_left != 0) {
                if (menu_selection == TITLE_ENTRY_NEW_GAME) {
                    data_fdps_roster_member_count = 0;
                    data_fdps_chapter_current_chapter_id =
                        TITLE_NEW_GAME_CHAPTER;
                    data_fdps_ui_terrain_hud_user_enabled = 0;
                    (*data_fdps_chapter_init_handler_table[
                        data_fdps_chapter_current_chapter_id])();
                    data_fdps_ui_terrain_hud_user_enabled = 1;
                    selection_done = 1;
                } else if (menu_selection == TITLE_ENTRY_LOAD_GAME) {
                    load_screen_page =
                        (unsigned char *) malloc((size_t) VGA_SCREEN_BYTES);
                    memset(load_screen_page, 0, (size_t) VGA_SCREEN_BYTES);
                    selection_done = fdps_load_game_screen(load_screen_page);
                    if (selection_done == TITLE_LOAD_CANCELLED) {
                        selection_done = 0;
                    }
                    free(load_screen_page);
                    data_fdps_village_skip_save_prompt_flag = 1;
                    if (selection_done == TITLE_LOAD_ACCEPTED) {
                        fdps_run_village_phase();
                    }
                } else if (menu_selection == TITLE_ENTRY_CONTINUE) {
                    fdps_load_savegame();
                    selection_done = 1;
                } else {
                    selection_done = 1;
                    data_fdps_shared_quit_game_requested = 1;
                }
                data_fdps_map_cursor_draw_mode = TITLE_CURSOR_DRAW_MODE_ON;
            }
            if (idle_ticks_left == 0) {
                selection_done = 1;
            }
        }

        if (idle_ticks_left == 0) {
            fdps_title_demo();
        } else {
            title_over = 1;
        }
        movie_index = (movie_index + 1) % TITLE_MOVIE_COUNT;
    }

    return menu_selection;
}
