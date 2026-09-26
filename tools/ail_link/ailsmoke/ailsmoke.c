/* ailsmoke.c -- link and run check for the AIL library carried over from FD2.
 *
 * Answers two questions the knowledge base could only assert before:
 *   1. does the library link into an FDPS-flagged DOS/4G image with nothing
 *      undefined, once the game side supplies its own DPMI routines?
 *   2. does audio actually initialise and play inside DOSBox-X?
 *
 * The entry points exercised here are the eighteen FDPS's own code calls, so
 * a pass covers the whole surface the rebuilt game will use rather than a
 * convenient subset.
 *
 * Two of them earn their place for what they drag in behind them. Playing a
 * real tone rather than checking that AIL_install_DIG_INI returned non-NULL:
 * installing the driver only proves the INI was parsed, while a sample that
 * reaches "playing" and then "done" has gone through the DMA buffers and the
 * mixer. And registering a timer callback rather than trusting the handle:
 * the game's own clock is a callback that fdps_audio_timer_install hands to
 * AIL_register_timer, so this proves an application-registered callback is
 * really invoked from the timer ISR. The ISR path itself is already reached
 * by the tone: installing the DIG driver registers and starts
 * AIL_internal_audio_mix_isr as a timer callback, so the mixer runs inside
 * the same ISR the game-side DPMI locking exists to protect.
 *
 * Every observation goes to RESULT.TXT as key=value; the host driver reads
 * that file, not the exit status.
 */
#include <stdio.h>
#include <stdlib.h>
#include "ailv3.h"

#define SAMPLE_RATE   11025
#define TONE_HZ         440
#define TONE_MS         400
#define TONE_BYTES    (SAMPLE_RATE * TONE_MS / 1000)

#define DIG_F_MONO_8      0   /* unsigned 8-bit mono PCM */
#define SMP_DONE          2
#define SMP_PLAYING       4
#define TIMER_HZ         50

static FILE *result;
static volatile unsigned tick_count;

static void say(const char *key, const char *value)
{
    fprintf(result, "%s=%s\n", key, value);
    fflush(result);
}

static void say_int(const char *key, int value)
{
    fprintf(result, "%s=%d\n", key, value);
    fflush(result);
}

static void __cdecl on_timer(unsigned int timer_handle)
{
    (void)timer_handle;
    tick_count++;
}

/* A square wave rather than a sine: no floating point, and every byte is one
   of two values, so a mis-set sample format shows up as silence or noise
   instead of something that plays either way. */
static void fill_tone(unsigned char *buf, unsigned len)
{
    unsigned i;
    unsigned half = SAMPLE_RATE / (TONE_HZ * 2);

    if (half == 0) {
        half = 1;
    }
    for (i = 0; i < len; i++) {
        buf[i] = ((i / half) & 1) ? 0xc0 : 0x40;
    }
}

static int play_tone(HDIGDRIVER dig, unsigned char *tone)
{
    HSAMPLE sample;
    int status, saw_playing, spins;

    sample = AIL_allocate_sample_handle(dig);
    if (sample == NULL) {
        say("sample", "fail");
        return 0;
    }
    say("sample", "ok");

    AIL_init_sample(sample);
    AIL_set_sample_address(sample, (unsigned)tone, TONE_BYTES);
    AIL_set_sample_type(sample, DIG_F_MONO_8, 0);
    AIL_set_sample_playback_rate(sample, SAMPLE_RATE);
    AIL_set_sample_volume(sample, 96);
    AIL_set_sample_loop_count(sample, 1);
    AIL_start_sample(sample);

    /* Poll rather than sleep: reaching PLAYING is the interesting event, and
       waiting a fixed interval would report the same thing whether the mixer
       ran or never started. */
    saw_playing = 0;
    status = 0;
    for (spins = 0; spins < 200; spins++) {
        status = AIL_sample_status(sample);
        if (status == SMP_PLAYING) {
            saw_playing = 1;
        }
        if (saw_playing && status != SMP_PLAYING) {
            break;
        }
        AIL_delay(1);
    }
    say("playing", saw_playing ? "ok" : "never");
    say_int("final_status", status);
    say_int("active_count", AIL_active_sample_count(dig));

    AIL_stop_sample(sample);
    AIL_release_sample_handle(sample);
    return saw_playing && status == SMP_DONE;
}

static int run_timer(void)
{
    HTIMER timer;
    int spins;

    timer = AIL_register_timer((unsigned int)on_timer);
    if (timer == (HTIMER)-1) {
        say("timer", "fail");
        return 0;
    }
    tick_count = 0;
    AIL_set_timer_frequency(timer, TIMER_HZ);
    AIL_start_timer(timer);
    for (spins = 0; spins < 100 && tick_count == 0; spins++) {
        AIL_delay(1);
    }
    AIL_stop_timer(timer);
    AIL_release_timer_handle(timer);
    say("timer", tick_count ? "ok" : "never");
    say_int("timer_ticks", (int)tick_count);
    return tick_count != 0;
}

int main(void)
{
    HDIGDRIVER dig;
    HMDIDRIVER mdi;
    HSEQUENCE sequence;
    unsigned char *tone;
    int tone_ok, timer_ok;

    result = fopen("RESULT.TXT", "w");
    if (result == NULL) {
        return 1;
    }
    say("stage", "start");

    tone = (unsigned char *)malloc(TONE_BYTES);
    if (tone == NULL) {
        say("verdict", "no_memory");
        fclose(result);
        return 1;
    }
    fill_tone(tone, TONE_BYTES);

    AIL_startup();
    say("startup", "ok");

    dig = AIL_install_DIG_INI();
    if (dig == NULL) {
        say_int("dig_error", AIL_get_last_error_code());
        say("dig", "fail");
        say("verdict", "fail");
        AIL_shutdown();
        fclose(result);
        return 1;
    }
    say("dig", "ok");

    tone_ok = play_tone(dig, tone);
    timer_ok = run_timer();

    /* FDPS ships no MDI.INI and no .MDI driver -- music is CD audio -- so the
       game's own AIL_install_MDI_INI call returns NULL on a stock install.
       Reported, not required: a pass here would mean the test environment is
       not the shipping one. */
    mdi = AIL_install_MDI_INI();
    say("mdi", mdi ? "installed" : "absent");
    if (mdi != NULL) {
        sequence = AIL_allocate_sequence_handle(mdi);
        say("sequence", sequence ? "ok" : "fail");
        if (sequence != NULL) {
            AIL_release_sequence_handle(sequence);
        }
        AIL_uninstall_MDI_driver(mdi);
    }

    AIL_uninstall_DIG_driver(dig);
    AIL_shutdown();
    free(tone);
    say("shutdown", "ok");

    say("verdict", (tone_ok && timer_ok) ? "ok" : "fail");
    fclose(result);
    return 0;
}
