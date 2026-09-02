/* title.c -- the front-of-house screens: the title menu and its demo, the
 * game-over screen, the ending credit roll, and the full-screen FMV playback
 * they reach for.
 *
 * See title.h for what each entry point promises its caller.  This file holds
 * no state of its own: everything it touches belongs to another module or to
 * the machine.
 *
 * sprintf comes from <stdio.h>, memset from <string.h>, kbhit and getch from
 * <conio.h> and spawnlp from <process.h>, and every one of them is a real
 * library call in the image rather than an inline expansion -- CALL 00042d41,
 * CALL 00042cd0, CALL 00043570, CALL 00043587 and CALL 000435a2 -- because the
 * flag that would inline the string and character routines, -oi, is not in this
 * build's set (rebuild_info/build_flags.md).  <stddef.h> is here for NULL, the
 * argument-list terminator spawnlp wants.
 *
 * The Miles library is reached only through its public entry AIL_shutdown; the
 * bring-up on the way back is audio.c's business and goes through
 * fdps_audio_init.  rebuild_info/ail_link.md has the linking contract.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <conio.h>
#include <process.h>
#include "ailv3.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "audio.h"
#include "cdaudio.h"
#include "keybd.h"
#include "palette.h"
#include "title.h"

/* The VGA graphics aperture as a flat linear address and the size of one whole
   mode 13h frame.  Both are hard-coded in the original (PUSH 0xa0000, PUSH
   0xfa00 at 00030ff2 and 00030ff9) and stay literals here: 0xa0000 is where
   the display adapter answers, not the address of anything the linker places,
   so there is no symbol to reference instead. */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_BYTES 0xfa00

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
