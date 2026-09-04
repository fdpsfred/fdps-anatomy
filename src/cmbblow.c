/* cmbblow.c -- one blow of the full-screen animated attack exchange.
 *
 * See cmbblow.h for what a caller has to know, src/combat.h for the exchange
 * that drives this and for the arithmetic behind a blow, and
 * resource_info/saf.md for the clips it plays.  This file owns no state at all:
 * everything it works on is an argument, a record reached through one, or the
 * page it allocates for itself.
 *
 * malloc and free come from <stdlib.h>, rand from <stdlib.h> as well, memset
 * from <string.h>, inp and outp from <conio.h> and delay from <i86.h>, which is
 * where Watcom 10.0a declares each of them.  All six are real calls in the
 * original -- CALL 0x0003d375 at 00019717, CALL 0x0003d478 at 00019f66, CALL
 * 0x00042cf8 at 00019802 and 0001982e, CALL 0x00042cd0 at 000198f0, CALL
 * 0x0003d4e4 at 0001995c and CALL 0x00042cb8 at 00019ce4, CALL 0x0003d370 at
 * 00019d1b -- because the flag set carries no -oi, so __INLINE_FUNCTIONS__ is
 * not defined and the plain declarations are what reproduce them
 * (rebuild_info/build_flags.md).
 */
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <i86.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "blit.h"
#include "sprite.h"
#include "saf.h"
#include "gauge.h"
#include "unit.h"
#include "unititem.h"
#include "table.h"
#include "audio.h"
#include "combat.h"
#include "cmbblow.h"

/* The page every frame is composed on: 368 x 248 with a 24-pixel apron on
   every side, PUSH 0x16480 at 00019712 with the pitch and the row count stored
   into the draw request at 00019725 and 0001972f.  The apron is what lets the
   struck sprite be thrown off the visible edge without being clipped. */
#define COMBAT_PAGE_PITCH 0x170
#define COMBAT_PAGE_ROWS 0xf8
#define COMBAT_PAGE_BYTES 0x16480
#define COMBAT_PAGE_MARGIN 0x18

/* The window handed to the adapter after every frame: 320 x 200 taken from
   page byte 0x2298, which is page pixel (24,24), and put down at the top left
   of the screen.  All six figures come from the pushes at 0001997e. */
#define COMBAT_PAGE_WINDOW_AT 0x2298
#define COMBAT_WINDOW_W 0x140
#define COMBAT_WINDOW_H 0xc8

/* 0xa0000 stays a literal because it is where the adapter answers and not the
   address of anything the linker places (rebuild_info/pitfalls.md,
   contract E). */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140

#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* The critical flash, PUSH 0x3c8 / PUSH 0x3c9 at 00019cdf onwards: DAC entry 0
   is driven to full white, held, and written back as black. */
#define VGA_DAC_WRITE_INDEX 0x3c8
#define VGA_DAC_DATA 0x3c9
#define VGA_DAC_MAX_COMPONENT 0x3f
#define CRITICAL_FLASH_ENTRY 0
#define CRITICAL_FLASH_MS 0x32

/* The two recoil ramps, the dword[6] templates at 00018c60 and 00018c78 that
   REP MOVSD copies onto the frame at 000196e9 and 000196fb -- which is what an
   initialised automatic array compiles to.  Neither is a global and neither has
   another reader.  The counter walks 5 down to 0, so entry 5 is the moment of
   impact and entry 0 is rest. */
#define RECOIL_STEPS 6
#define RECOIL_START 5

/* How many frames close the animation once the clip has run out, CMP dword ptr
   [EBP-0x34],0xc at 00019e04. */
#define SETTLE_FRAMES 12

/* What the request's two blend slots take while the struck sprite is drawn,
   MOV ...,0x28000 and MOV ...,0x3 at 00019b55: blit mode 3 for the whole
   sprite, in place of each part's own mode (src/sprite.h). */
#define STRUCK_BLEND_OPERAND 0x28000
#define STRUCK_BLIT_MODE 3

/* The double strike: hit_effect 2 of the attacker's weapon, or either of two
   rolls of rand() % 100 against 3. */
#define PERCENT 100
#define DOUBLE_STRIKE_PERCENT 3
#define DOUBLE_STRIKE_BLOWS 2
#define WEAPON_EFFECT_DOUBLE_STRIKE 2

/* The three slots of the outcome block this function reads.  The block is six
   ints and fdps_combat_compute_hit_outcome (src/combat.h) writes all of them;
   the three between the flags and the damage are read by nothing. */
#define OUTCOME_SLOTS 6
#define OUTCOME_MISSED 0
#define OUTCOME_CRITICAL 1
#define OUTCOME_DAMAGE 5

/* The two bytes of a .SAF frame record this function reads, both widened
   UNSIGNED by the XOR EAX,EAX / MOV AL pairs at 000198ad and 000197f7: the
   travelling-attack lead-in count, read off frame 0 only, and the impact
   marker, read off every frame (resource_info/saf.md). */
#define SAF_FRAME_LEAD_IN_AT 4
#define SAF_FRAME_IMPACT_AT 5

/* It has to be a plain writable literal.  The lookup inside fdps_play_sfx
   upper-cases the caller's own storage in place (src/vfs.h), so this spelling
   is permanently "MISS.WAV" after the first missed blow of the run, exactly as
   the original's copy at 0x616f8 is (rebuild_info/pitfalls.md). */
#define BLOW_MISS_SOUND "Miss.wav"

/* 000196d0.  The frame is the plain -od one -- PUSH EBX/ESI/EDI/EBP / MOV
   EBP,ESP / SUB ESP,0xd4 -- and both call sites in
   fdps_combat_play_attack_exchange, at 000190d9 and 00019118, push seven dwords
   and follow the CALL with ADD ESP,0x1c, so the caller clears the arguments and
   the answer comes back in EAX, where TEST EAX,EAX at 000190e1 reads it.
   Nothing here is __watcall whatever Ghidra's automatic signature says.

   BOTH DOUBLE-STRIKE ROLLS ARE TAKEN, ALWAYS.  The two CALL 0x00042cf8 at
   00019802 and 0001982e sit either side of the CMP dword ptr [EBP-0x14],0x2
   that tests the weapon, and neither is under a branch: a blow already promoted
   to two by the first roll still takes the second.  That is not a redundancy to
   fold away.  The real double-strike rate of a plain weapon is 1 - 0.97 * 0.97
   = 5.91% and not 3%, and one rand() fewer shifts every later roll of the
   battle (rebuild_info/pitfalls.md).

   THE RETURNED HP IS DELIBERATELY LEFT UNINITIALISED until an impact frame
   writes it.  [EBP-0x54] is stored only inside the impact arm at 00019a6d, and
   it is read at 00019d82 to decide whether the defender died and at 00019f6e as
   the answer, so a clip whose every frame carries a zero impact marker returns
   whatever the slot held.  Giving it an initialiser would make a clip that
   cannot happen in the shipped data return a number the original does not.

   THE PER-TICK LOOP RUNS AT LEAST ONCE.  It is a do/while whose test is
   fdps_saf_advance_tick's answer at 00019d72, so the branch structure is the
   assembly's and not a while() that would skip an empty clip.

   THE SECOND BLOW IS DECIDED AFTER THE FIRST HAS BEEN PLAYED, from the counter
   the loop head already decremented: CMP dword ptr [EBP-0x50],0x0 at 00019d8f
   asks whether another blow is coming, which is why a single blow that
   travelled never slides the view back and the settle frames play over the
   defender's backdrop.

   data_fdps_timer_tick_counter is volatile at its declaration (gamedata.h)
   because of the three tick waits: nothing inside them writes the counter, so a
   build allowed to hoist the load would spin here forever.  The retrace spins
   read a port and cannot be hoisted for the same reason.

   SIX CALLS' ANSWERS ARE READ.  malloc's is the page and is not tested;
   fdps_saf_frame_count's bounds the impact-marker count; fdps_saf_get_frame's
   is the frame record both bytes come out of; the two fdps_get_unit_record
   pointers are the records the side byte and the HP word are reached through;
   fdps_unit_find_equipped_slot's answer goes straight into
   fdps_unit_get_item_id and its answer into fdps_get_item_record, whose pointer
   supplies the weapon effect; rand's is taken modulo 100 twice; and
   fdps_saf_advance_tick's answer is read at the bottom of the per-tick loop and
   discarded at its other four call sites.  inp's is tested for bit 3 at all six
   spins.  fdps_combat_compute_hit_outcome, fdps_draw_composite_sprite,
   fdps_draw_unit_hp_mp_gauges, fdps_combat_slide_backdrops, fdps_blit_rect,
   fdps_play_sfx, memset, outp, delay and free return nothing the original
   reads.

   The frames are paced by the retrace and by the timer tick, so how many
   instructions stand between them is not observable (contract D). */
int fdps_combat_play_blow(int attacker_unit_index, int defender_unit_index,
                          void *act_clip, int *attacker_stand_cursor,
                          int *defender_stand_cursor, void *from_backdrop,
                          void *to_backdrop)
{
    /* How far the struck sprite is thrown sideways and upwards at each step of
       the recoil.  Two separate templates in the original and two separate
       arrays here; nothing indexes from one into the other. */
    int recoil_x_ramp[RECOIL_STEPS] = {0, 4, 9, 14, 18, 14};
    int recoil_y_ramp[RECOIL_STEPS] = {0, 2, 4, 6, 8, 10};
    /* The block sprite.h describes.  Slot 0 is the page -- there is no page
       variable, the pointer lives here and nowhere else, exactly as malloc's
       answer is stored at 0001971f and read back at 000198e9, 00019997 and
       00019f5f. */
    int request[DRAW_REQUEST_DWORDS];
    /* What fdps_combat_compute_hit_outcome fills in for this blow. */
    int outcome[OUTCOME_SLOTS];
    /* The playback cursor over act_clip, built here and primed by the reset
       call; the two stand cursors belong to the caller. */
    int act_cursor[SAF_CURSOR_DWORDS];
    /* The two records, resolved once each before the first blow. */
    struct fdps_unit_record *attacker;
    struct fdps_unit_record *defender;
    /* The attacker's equipped weapon.  The original keeps the slot number and
       then the item id in the one slot [EBP-0x18]; they are two names here
       because the slot is an inventory position and the id is an item. */
    int weapon_slot;
    int weapon_item_id;
    struct fdps_item_effect *weapon;
    /* The weapon's hit-effect byte, zero-extended at 000197f7. */
    int weapon_hit_effect;
    /* How many frames the clip holds, and how many of them carry the impact
       marker -- forced to 1 when none does, so the division below cannot be by
       zero. */
    int act_frame_count;
    int impact_frame_count;
    /* How many blows are still to be struck, counted down at the loop head; the
       loop ends when it reaches -1. */
    int blows_remaining;
    /* How many impact frames of this blow have been paid out so far. */
    int blows_paid;
    /* Where the recoil ramps are read: set to 5 by a landed blow and walked
       back down to 0 one step per frame. */
    int recoil_counter;
    /* -1 until the impact frame of a landed blow sets it to 1, which is what
       puts the struck sprite through the single blit mode. */
    int blow_landed;
    /* The defender's HP as this blow started, and what is left of it.  The
       second is deliberately not initialised -- see the note above. */
    int start_hp;
    int remaining_hp;
    /* The vertical throw this frame, which is the ramp entry on a blow that
       does not travel and 0 on one that does. */
    int recoil_y;
    /* Which way the view slides, and which way the defender is thrown; the two
       come off the same side byte with opposite signs. */
    int slide_direction;
    int recoil_sign;
    /* The tick the previous frame ended on.  Deliberately not initialised, the
       same contract the animations in src/anim.c carry, so the first frame of
       the blow does not wait. */
    unsigned int last_tick;
    /* Whether the act clip's own frame sound is allowed to play: cleared for
       the impact frame of a missed blow, which is what suppresses the hit
       sound the clip carries. */
    int clip_plays_sound;
    /* Frame 0's lead-in count: how many frames play before the view travels,
       and 0 for an attack that does not travel at all. */
    int lead_in_frames;
    /* The frame record the two marker bytes are read out of. */
    unsigned char *current_frame;
    /* The counting loop's index, and then the settle loop's. */
    int i;
    /* The three-step swap of the two backdrop arguments. */
    void *swap_backdrop;

    blows_remaining = 1;
    impact_frame_count = 0;
    recoil_counter = 0;

    request[DRAW_REQUEST_DEST_BASE] = (int) malloc((size_t) COMBAT_PAGE_BYTES);
    request[DRAW_REQUEST_DEST_PITCH] = COMBAT_PAGE_PITCH;
    request[DRAW_REQUEST_DEST_ROWS] = COMBAT_PAGE_ROWS;
    request[DRAW_REQUEST_BLIT_OPERAND] = 0;
    request[DRAW_REQUEST_BLIT_MODE] = 0;

    /* Every frame of the clip is looked at, including the lead-in ones -- so a
       clip whose impact markers sit inside its lead-in counts them here and
       never pays them, and the defender loses only the share of the damage the
       frames that did run were worth. */
    act_frame_count = fdps_saf_frame_count(act_clip);
    for (i = 0; i < act_frame_count; i++) {
        current_frame = (unsigned char *) fdps_saf_get_frame(act_clip, i);
        if (current_frame[SAF_FRAME_IMPACT_AT] != 0) {
            impact_frame_count++;
        }
    }
    if (impact_frame_count == 0) {
        impact_frame_count = 1;
    }

    attacker = fdps_get_unit_record(attacker_unit_index);
    defender = fdps_get_unit_record(defender_unit_index);

    weapon_slot = fdps_unit_find_equipped_slot(attacker_unit_index, 0);
    weapon_item_id = fdps_unit_get_item_id(attacker_unit_index, weapon_slot);
    weapon = fdps_get_item_record(weapon_item_id);
    weapon_hit_effect = (int) weapon->hit_effect;

    /* Both rolls, unconditionally, either side of the weapon test -- see the
       note above. */
    if (rand() % PERCENT < DOUBLE_STRIKE_PERCENT) {
        blows_remaining = DOUBLE_STRIKE_BLOWS;
    }
    if (weapon_hit_effect == WEAPON_EFFECT_DOUBLE_STRIKE) {
        blows_remaining = DOUBLE_STRIKE_BLOWS;
    }
    if (rand() % PERCENT < DOUBLE_STRIKE_PERCENT) {
        blows_remaining = DOUBLE_STRIKE_BLOWS;
    }

    for (;;) {
        blows_remaining--;
        if (blows_remaining == -1) {
            break;
        }

        blows_paid = 0;
        start_hp = (int) (unsigned short) defender->hp_current;
        fdps_combat_compute_hit_outcome(attacker_unit_index,
                                        defender_unit_index, outcome);

        act_cursor[SAF_CURSOR_IMAGE] = (int) act_clip;
        fdps_saf_advance_tick(act_cursor, 1);
        current_frame = (unsigned char *) fdps_saf_get_frame(act_clip, 0);
        lead_in_frames = (int) current_frame[SAF_FRAME_LEAD_IN_AT];

        if (lead_in_frames != 0) {
            request[DRAW_REQUEST_X] = COMBAT_PAGE_MARGIN;
            request[DRAW_REQUEST_Y] = COMBAT_PAGE_MARGIN;
            while (act_cursor[SAF_CURSOR_FRAME_INDEX] < lead_in_frames) {
                memset((void *) request[DRAW_REQUEST_DEST_BASE], 0,
                       (size_t) COMBAT_PAGE_BYTES);
                request[DRAW_REQUEST_IMAGE] = (int) from_backdrop;
                request[DRAW_REQUEST_ITEM_INDEX] = 0;
                fdps_draw_composite_sprite(request, 0);
                request[DRAW_REQUEST_IMAGE] = (int) act_clip;
                request[DRAW_REQUEST_ITEM_INDEX] =
                    act_cursor[SAF_CURSOR_FRAME_INDEX];
                fdps_draw_composite_sprite(request, 1);
                fdps_draw_unit_hp_mp_gauges(
                    (unsigned char *) request[DRAW_REQUEST_DEST_BASE],
                    COMBAT_PAGE_PITCH, attacker_unit_index);
                while ((inp(VGA_INPUT_STATUS_1)
                        & VGA_STATUS_VERTICAL_RETRACE) == 0) {
                    /* Spin until the retrace begins, so the frame that has just
                       been composed is the one the monitor shows whole. */
                }
                while ((inp(VGA_INPUT_STATUS_1)
                        & VGA_STATUS_VERTICAL_RETRACE) != 0) {
                    /* And until it ends, so the blit starts clear of it. */
                }
                fdps_blit_rect(
                    (unsigned int)
                        ((unsigned char *) request[DRAW_REQUEST_DEST_BASE]
                         + COMBAT_PAGE_WINDOW_AT),
                    COMBAT_PAGE_PITCH, (void *) VGA_SCREEN_BASE,
                    VGA_SCREEN_PITCH, COMBAT_WINDOW_W, COMBAT_WINDOW_H);
                while (last_tick == data_fdps_timer_tick_counter) {
                }
                last_tick = data_fdps_timer_tick_counter;
                fdps_saf_advance_tick(act_cursor, 0);
            }

            if (attacker->side == 0) {
                slide_direction = -1;
            } else {
                slide_direction = 1;
            }
            fdps_combat_slide_backdrops(from_backdrop, to_backdrop,
                                        defender_stand_cursor, request,
                                        slide_direction);
            swap_backdrop = from_backdrop;
            from_backdrop = to_backdrop;
            to_backdrop = swap_backdrop;
        }

        do {
            /* Only the first tick a frame is held for pays anything and only it
               re-reads the frame record; a frame whose duration is more than
               one tick keeps the record and the flags the first tick left. */
            if (act_cursor[SAF_CURSOR_TICKS_HELD] == 0) {
                current_frame = (unsigned char *)
                    fdps_saf_get_frame(act_clip,
                                       act_cursor[SAF_CURSOR_FRAME_INDEX]);
                blow_landed = -1;
                clip_plays_sound = 1;
                if (current_frame[SAF_FRAME_IMPACT_AT] != 0) {
                    blows_paid++;
                    remaining_hp = start_hp
                                   - blows_paid * outcome[OUTCOME_DAMAGE]
                                     / impact_frame_count;
                    if (remaining_hp < 0) {
                        remaining_hp = 0;
                    }
                    defender->hp_current = (short) remaining_hp;
                    if (outcome[OUTCOME_MISSED] == 0) {
                        recoil_counter = RECOIL_START;
                        blow_landed = 1;
                    } else {
                        clip_plays_sound = 0;
                        fdps_play_sfx(BLOW_MISS_SOUND);
                    }
                }
            }

            /* A travelling attack shows no vertical throw at all: the ramp is
               read only when there is no lead-in. */
            if (lead_in_frames != 0) {
                recoil_y = 0;
            } else {
                recoil_y = recoil_y_ramp[recoil_counter];
            }

            memset((void *) request[DRAW_REQUEST_DEST_BASE], 0,
                   (size_t) COMBAT_PAGE_BYTES);
            request[DRAW_REQUEST_X] = COMBAT_PAGE_MARGIN;
            request[DRAW_REQUEST_Y] = COMBAT_PAGE_MARGIN;
            request[DRAW_REQUEST_IMAGE] = (int) from_backdrop;
            request[DRAW_REQUEST_ITEM_INDEX] = 0;
            fdps_draw_composite_sprite(request, 0);

            /* The defender is thrown away from the attacker, so the sign is the
               opposite of the one the slide takes off the same byte. */
            if (attacker->side == 0) {
                recoil_sign = 1;
            } else {
                recoil_sign = -1;
            }
            fdps_saf_advance_tick(defender_stand_cursor, 0);
            if (blow_landed < 0) {
                request[DRAW_REQUEST_BLIT_OPERAND] = 0;
                request[DRAW_REQUEST_BLIT_MODE] = 0;
            } else {
                request[DRAW_REQUEST_BLIT_OPERAND] = STRUCK_BLEND_OPERAND;
                request[DRAW_REQUEST_BLIT_MODE] = STRUCK_BLIT_MODE;
            }
            request[DRAW_REQUEST_X] =
                recoil_sign * recoil_x_ramp[recoil_counter]
                + COMBAT_PAGE_MARGIN;
            request[DRAW_REQUEST_Y] =
                recoil_sign * recoil_y + COMBAT_PAGE_MARGIN;
            request[DRAW_REQUEST_IMAGE] =
                defender_stand_cursor[SAF_CURSOR_IMAGE];
            request[DRAW_REQUEST_ITEM_INDEX] =
                defender_stand_cursor[SAF_CURSOR_FRAME_INDEX];
            fdps_draw_composite_sprite(request, 0);

            request[DRAW_REQUEST_BLIT_OPERAND] = 0;
            request[DRAW_REQUEST_BLIT_MODE] = 0;
            request[DRAW_REQUEST_X] = COMBAT_PAGE_MARGIN;
            request[DRAW_REQUEST_Y] = COMBAT_PAGE_MARGIN;
            request[DRAW_REQUEST_IMAGE] = (int) act_clip;
            request[DRAW_REQUEST_ITEM_INDEX] =
                act_cursor[SAF_CURSOR_FRAME_INDEX];
            fdps_draw_composite_sprite(request, (char) clip_plays_sound);

            if (lead_in_frames == 0) {
                fdps_draw_unit_hp_mp_gauges(
                    (unsigned char *) request[DRAW_REQUEST_DEST_BASE],
                    COMBAT_PAGE_PITCH, attacker_unit_index);
            }
            fdps_draw_unit_hp_mp_gauges(
                (unsigned char *) request[DRAW_REQUEST_DEST_BASE],
                COMBAT_PAGE_PITCH, defender_unit_index);

            while ((inp(VGA_INPUT_STATUS_1)
                    & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            }
            while ((inp(VGA_INPUT_STATUS_1)
                    & VGA_STATUS_VERTICAL_RETRACE) != 0) {
            }
            fdps_blit_rect(
                (unsigned int)
                    ((unsigned char *) request[DRAW_REQUEST_DEST_BASE]
                     + COMBAT_PAGE_WINDOW_AT),
                COMBAT_PAGE_PITCH, (void *) VGA_SCREEN_BASE, VGA_SCREEN_PITCH,
                COMBAT_WINDOW_W, COMBAT_WINDOW_H);
            while (last_tick == data_fdps_timer_tick_counter) {
            }
            last_tick = data_fdps_timer_tick_counter;

            /* The flash goes on the impact frame of a critical blow and is
               taken off again before the frame ends, so it is one frame of
               white whatever the machine's speed. */
            if (outcome[OUTCOME_CRITICAL] != 0
                    && current_frame[SAF_FRAME_IMPACT_AT] != 0
                    && act_cursor[SAF_CURSOR_TICKS_HELD] == 0) {
                outp(VGA_DAC_WRITE_INDEX, CRITICAL_FLASH_ENTRY);
                outp(VGA_DAC_DATA, VGA_DAC_MAX_COMPONENT);
                outp(VGA_DAC_DATA, VGA_DAC_MAX_COMPONENT);
                outp(VGA_DAC_DATA, VGA_DAC_MAX_COMPONENT);
                delay(CRITICAL_FLASH_MS);
                outp(VGA_DAC_WRITE_INDEX, CRITICAL_FLASH_ENTRY);
                outp(VGA_DAC_DATA, 0);
                outp(VGA_DAC_DATA, 0);
                outp(VGA_DAC_DATA, 0);
            }

            if (recoil_counter != 0) {
                recoil_counter--;
            }
        } while (fdps_saf_advance_tick(act_cursor, 0) == 0);

        if (remaining_hp == 0) {
            blows_remaining = 0;
        }
        if (blows_remaining != 0 && lead_in_frames != 0) {
            fdps_combat_slide_backdrops(from_backdrop, to_backdrop,
                                        attacker_stand_cursor, request,
                                        -slide_direction);
            swap_backdrop = from_backdrop;
            from_backdrop = to_backdrop;
            to_backdrop = swap_backdrop;
        }
    }

    request[DRAW_REQUEST_BLIT_OPERAND] = 0;
    request[DRAW_REQUEST_BLIT_MODE] = 0;
    request[DRAW_REQUEST_X] = COMBAT_PAGE_MARGIN;
    request[DRAW_REQUEST_Y] = COMBAT_PAGE_MARGIN;
    for (i = 0; i < SETTLE_FRAMES; i++) {
        memset((void *) request[DRAW_REQUEST_DEST_BASE], 0,
               (size_t) COMBAT_PAGE_BYTES);
        request[DRAW_REQUEST_IMAGE] = (int) from_backdrop;
        request[DRAW_REQUEST_ITEM_INDEX] = 0;
        fdps_draw_composite_sprite(request, 0);
        fdps_saf_advance_tick(defender_stand_cursor, 0);
        request[DRAW_REQUEST_IMAGE] = defender_stand_cursor[SAF_CURSOR_IMAGE];
        request[DRAW_REQUEST_ITEM_INDEX] =
            defender_stand_cursor[SAF_CURSOR_FRAME_INDEX];
        fdps_draw_composite_sprite(request, 0);
        fdps_draw_unit_hp_mp_gauges(
            (unsigned char *) request[DRAW_REQUEST_DEST_BASE],
            COMBAT_PAGE_PITCH, defender_unit_index);
        if (lead_in_frames == 0) {
            fdps_saf_advance_tick(attacker_stand_cursor, 0);
            request[DRAW_REQUEST_IMAGE] =
                attacker_stand_cursor[SAF_CURSOR_IMAGE];
            request[DRAW_REQUEST_ITEM_INDEX] =
                attacker_stand_cursor[SAF_CURSOR_FRAME_INDEX];
            fdps_draw_composite_sprite(request, 0);
            fdps_draw_unit_hp_mp_gauges(
                (unsigned char *) request[DRAW_REQUEST_DEST_BASE],
                COMBAT_PAGE_PITCH, attacker_unit_index);
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
        }
        fdps_blit_rect(
            (unsigned int) ((unsigned char *) request[DRAW_REQUEST_DEST_BASE]
                            + COMBAT_PAGE_WINDOW_AT),
            COMBAT_PAGE_PITCH, (void *) VGA_SCREEN_BASE, VGA_SCREEN_PITCH,
            COMBAT_WINDOW_W, COMBAT_WINDOW_H);
        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
    }

    free((void *) request[DRAW_REQUEST_DEST_BASE]);
    return remaining_hp;
}
