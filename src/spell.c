/* spell.c -- applying a cast spell's effect, and the cast presentation.
 *
 * See spell.h for what a caller has to know.  Nothing in this file owns state:
 * every record it works on is resolved on the spot through the accessors in
 * unit.h and table.h, so a call made after the unit array has moved sees the
 * array as it is at that moment.
 *
 * rand, malloc and free come from <stdlib.h>, memmove and memset from
 * <string.h> and inp and outp from <conio.h>, which is where Watcom 10.0a
 * declares each of them.  Every one is a real library call in the original
 * rather than an inline expansion -- CALL 00042cf8 at 000283ee, CALL 0003d375
 * at 0002864d and 0002869d, CALL 0003d478 at 000287a4, 00028858 and 00028864,
 * CALL 0003d514 at 000286b6, CALL 00042cd0 at 000287c7 and 00028893, CALL
 * 0003d4e4 at 00028741, 00028752, 000287f5 and 00028806, and the eight CALL
 * 00042cb8 per pass at 0002915a onwards -- because the flag that would inline
 * the string, character and port routines, -oi, is not in this build's set
 * (rebuild_info/build_flags.md).
 */
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "unit.h"
#include "table.h"
#include "unitstat.h"
#include "vfs.h"
#include "saf.h"
#include "blit.h"
#include "sprite.h"
#include "palette.h"
#include "mapdraw.h"
#include "spell.h"

/* Every ratio in here is a percentage: the magic resistance complement, the
   attack multiplier and the hit rate are all divided or drawn against 100.
   The literal is MOV EBX,0x64 before each IDIV. */
#define PERCENT 100

/* PROMAP.DAT row 0 is a default row, so the class record for a unit is looked
   up at its class code PLUS ONE.  Every caller of fdps_get_class_record in the
   image applies this bias (table.h); dropping it reads the previous class's
   magic resistance. */
#define CLASS_RECORD_BIAS 1

/* The two ground-shock spells, the only ids this function tests for.  Names
   from assets/spells.md, matching the defines in aiscore.c. */
#define SPELL_QUAKE 0x0a       /* 裂地術 */
#define SPELL_GREAT_QUAKE 0x0b /* 封神裂震 */

/* 00028320.  One spell landing on one unit.
 *
 * The three arguments arrive on the stack at [EBP+0x14], [EBP+0x18] and
 * [EBP+0x1c] and both call sites -- 0001ae5f and 00028e4f -- follow the CALL
 * with ADD ESP,0xc, so this is the stack convention with the caller cleaning
 * up, and the answer comes back in EAX (TEST EAX,EAX at 0001ae67).
 *
 * The two damage formulas are picked by JL at 0002837d on the spell record's
 * signed power word, MOVSX word ptr [EAX] at 00028373.  Both divides are
 * IDIV EBX with EBX 0x64 and EDX sign extended by SAR EDX,0x1f, so both are
 * signed, and the clamp on the second is JGE at 000283da -- signed as well.
 *
 * Three of the four record fields are byte loads preceded by XOR EAX,EAX, so
 * the class code at unit +0x20, the magic resistance complement at class
 * record +0x09 and the hit rate at spell record +0x02 are all zero extended
 * (0002833d, 00028358, 000283e8).  The two combat words are MOVSX: the
 * caster's attack at +0x48 (000283ac) and the target's defence at +0x4a
 * (000283c6).  struct fdps_unit_record and struct fdps_class_record carry
 * those signednesses, so the field types are the whole of it.
 *
 * The target's record pointer is taken once at 00028338 and the defence word
 * is read back off that same pointer at 000283c3, after the second
 * fdps_get_unit_record call for the caster -- the target is not re-resolved.
 *
 * Rebuild note: rand() is drawn at 000283ee, BEFORE the flying test at
 * 00028416, so a target that is immune to 裂地術 or 封神裂震 still consumes
 * one value from the PRNG.  Hoisting the immunity into a guard clause at the
 * top of the function -- the obvious shape for an immunity -- leaves that draw
 * untaken and shifts every later damage roll of the battle. */
int fdps_spell_damage_unit(int caster_unit_index, int target_unit_index,
                           int spell_id)
{
    struct fdps_unit_record *target;
    struct fdps_unit_record *caster;
    struct fdps_class_record *target_class;
    struct fdps_spell_effect *spell;
    int target_class_code;
    int magic_resist_complement;
    int spell_power;
    int attack_share;
    int target_defense;
    int damage;
    int hit_rate;

    target = fdps_get_unit_record(target_unit_index);
    target_class_code = target->clazz;
    target_class = fdps_get_class_record(target_class_code + CLASS_RECORD_BIAS);
    magic_resist_complement = target_class->magic_resist_complement;

    spell = fdps_get_spell_record(spell_id);
    spell_power = spell->power;

    if (spell_power >= 0) {
        /* A flat magic figure: the caster's record is never even resolved on
           this path. */
        damage = spell_power * magic_resist_complement / PERCENT;
    } else {
        /* NEG dword ptr [EBP + -0x10] at 00028397: the power is negated in
           place and the result is the multiplier in percent. */
        spell_power = -spell_power;
        caster = fdps_get_unit_record(caster_unit_index);
        attack_share = caster->ap * spell_power / PERCENT;
        target_defense = target->dp;
        damage = attack_share - target_defense;
        if (damage < 0) {
            damage = 0;
        }
    }

    hit_rate = spell->hit_rate;
    if (rand() % PERCENT < hit_rate) {
        if ((spell_id == SPELL_QUAKE || spell_id == SPELL_GREAT_QUAKE) &&
            fdps_unit_is_flying(target_unit_index) != 0) {
            return 0;
        }
        return fdps_unit_apply_damage(target_unit_index, damage);
    }

    return 0;
}

/* 00028570.  A spell's power fed straight into a unit as a heal.
 *
 * The frame is the plain -4s one: PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP,
 * SUB ESP,0xc, the two arguments read from [EBP+0x14] and [EBP+0x18], and a
 * bare RET, so the caller cleans up and the answer comes back in EAX.  This
 * function's own two calls confirm the same convention from the other side --
 * ADD ESP,0x4 after 00018bd0 and ADD ESP,0x8 after 00027070.
 *
 * There are no branches at all: fetch the record, widen its power word, hand
 * that to fdps_unit_apply_heal, return what that returned.  The value at
 * [EBP-0x4] that the epilogue loads into EAX is the CALL's own EAX stored at
 * 000285a4, so the return really is the callee's and not a recomputation.
 *
 * The power word is read with MOVSX word ptr [EAX] at 0002858e and passed
 * through with no sign test.  Eight spells store that field negative -- it
 * doubles as an attack-power multiplier in percent for the special attacks
 * (assets/tables/spells.md) -- and fdps_unit_apply_heal has no floor, so one
 * of those ids arriving here takes HP off instead of putting it on.  That is
 * what the original does; there is no guard to restore.
 *
 * Nothing else of the record is looked at: not the hit rate at +0x02, not the
 * MP cost at +0x05 (fdps_spell_deduct_mp_cost is the one that spends it) and
 * not the target side at +0x06.  spell_id is used for nothing but selecting
 * the record.
 *
 * Nothing in the image reaches this entry -- no call, no data word, no other
 * reference.  The two live heal paths call fdps_unit_apply_heal themselves, so
 * this is an unused packaged form of that step; its damage counterpart
 * fdps_spell_damage_unit above has two callers. */
int fdps_spell_heal_unit(int unit_index, int spell_id)
{
    struct fdps_spell_effect *spell;
    int heal_power;

    spell = fdps_get_spell_record(spell_id);
    heal_power = spell->power;

    return fdps_unit_apply_heal(unit_index, heal_power);
}

/* 000285c0.  Charging one action's MP cost to the unit that performed it.
 *
 * The frame is the plain -4s one -- PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP,
 * SUB ESP,0xc -- with the two arguments read from [EBP+0x14] and [EBP+0x18]
 * and a bare RET, so the caller cleans up.  The one call site at 0001ad6c
 * pushes [EBP+0x18] then [EBP+0x14] and follows the CALL with ADD ESP,0x8,
 * which is the same convention seen from the other side, and the instruction
 * after it loads [EBP-0x20] rather than EAX, so nothing is returned.
 *
 * There are no branches: two record lookups, one read, one subtract, one store.
 * The order of the two calls is the assembly's -- the spell record at 000285d0
 * before the unit record at 000285df -- and neither has an effect the other can
 * see, so the two pointers are independent.
 *
 * The two field widths are the whole of the arithmetic.  MOVSX word ptr
 * [EAX+0x44] at 000285ed widens the caster's current MP as a signed word, and
 * XOR EDX,EDX / MOV DL,byte ptr [EAX+0x5] at 000285f7 takes the record's cost
 * byte zero-extended, so a cost byte of 0xff is 255 and not -1.  Both
 * signednesses are already carried by struct fdps_unit_record's mp_current and
 * struct fdps_spell_effect's mp_cost, so the field types are all that is
 * needed.  MOV word ptr [EAX+0x44],BX stores the low 16 bits of the int-width
 * difference back.
 *
 * Rebuild note: nothing here tests whether the caster can afford the cost, and
 * there is no floor at zero -- a cost above the current MP leaves the field
 * negative, and a difference outside 16 bits wraps into the word rather than
 * saturating.  The maximum MP at record +0x46 is never read.  Adding the
 * affordability test that the shape of the function invites would change what
 * the game does; the caller does not make it either.
 *
 * The one caller, fdps_combat_play_spell_on_targets, runs this once after the
 * action's animation loop has finished -- the loop's back edge is the JMP to
 * 0001abc1 at 0001ad5f and the argument setup at 0001ad64 is where it lands on
 * exit.  Everything that reaches this function is a cast: that caller is
 * itself reached only from fdps_battle_spell_command (00027c20, call site
 * 00028083) and fdps_map_actor_cast_chosen_spell (00013c90, call site
 * 00013d89), and no plain-attack path leads into it.  Nor is a free action
 * expressible here -- the smallest MP cost in MAGICDAT.DAT is 4
 * (assets/spells.md), so the subtraction always takes something off. */
void fdps_spell_deduct_mp_cost(int unit_index, int spell_id)
{
    struct fdps_spell_effect *spell;
    struct fdps_unit_record *caster;
    int current_mp;

    spell = fdps_get_spell_record(spell_id);
    caster = fdps_get_unit_record(unit_index);
    current_mp = caster->mp_current;

    caster->mp_current = (short) (current_mp - spell->mp_cost);
}

/* The container spell 11's animation lives in and the member itself, from
   MOV EAX,0x60128 at 00028629 and MOV EAX,0x61bb0 at 00028623.  The member
   name reaches fdps_vfs_load_entry, which upper-cases its argument IN PLACE
   before the compare, so this literal is permanently folded to MAG11.SAF by
   the first call and cannot live in read-only storage (vfs.h,
   rebuild_info/pitfalls.md).  The container name is copied raw and is left as
   it stands. */
#define SPELL11_ARCHIVE "MISC.VFS"
#define SPELL11_CLIP "Mag11.saf"

/* The page every frame is composed on: 368 x 248 8bpp, PUSH 0x16480 at
   00028648.  It is 48 pixels wider and taller than the screen, so a frame may
   overhang the visible window without being clipped. */
#define SPELL11_PAGE_PITCH 0x170
#define SPELL11_PAGE_ROWS 0xf8
#define SPELL11_PAGE_BYTES 0x16480

/* Where the animation origin is put inside that page -- PUSH 0x18 twice at
   0002866c and 00028673 -- and the byte the visible 320 x 200 window starts
   at, which is the same corner: 24 * 368 + 24 = 0x2298, the ADD EAX,0x2298 at
   00028701, 0002877a and 0002882e. */
#define SPELL11_PAGE_MARGIN 0x18
#define SPELL11_PAGE_WINDOW_AT 0x2298

/* The mode 13h frame.  0xa0000 is where the display adapter answers, not the
   address of anything the linker places, so it stays a literal. */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140
#define VGA_SCREEN_ROWS 0xc8
#define VGA_SCREEN_BYTES 0xfa00

/* Input Status 1, whose bit 3 is set while the vertical retrace is running. */
#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* The blit the composite drawer is asked for and the three-dword descriptor
   that mode reads (rleblend.h).  The mode is set once, before the first frame,
   and neither phase changes it, so every layer of every frame is drawn
   translucent -- fdps_draw_composite_sprite consults a layer's own blend flag
   only when the request's mode is 0 (sprite.h). */
#define BLIT_MODE_TRANSLUCENT 9
#define BLEND_DESC_SHADE_RAMP 0
#define BLEND_DESC_LEVEL 1
#define BLEND_DESC_CUBE 2
#define BLEND_DESC_DWORDS 3

/* The fade-in: MOV dword ptr [EBP-0x10],0x1f at 000286c5 with CMP against 0
   and JG, so 31 ticks with the counter running 0x1f down to 1, and the blend
   level is its signed half -- SAR EDX,0x1f / SUB / SAR EAX,1 at 000286e5. */
#define SPELL11_FADE_TICKS 0x1f
#define SPELL11_FADE_LEVEL_DIVISOR 2

/* The white flash and the six steps that come out of it: the whole DAC every
   time, PUSH 0xff / PUSH 0x0 at 00028872 and 000288c1; three PUSH 0x3f at
   0002886c for the flash; and IMUL EAX,[EBP-0x10],0xa at 000288b2 with the
   counter starting at 5 and CMP against 0 / JGE, so six steps of 50, 40, 30,
   20, 10 and 0. */
#define FADE_FIRST_DAC_ENTRY 0
#define FADE_LAST_DAC_ENTRY 0xff
#define WHITE_FLASH_BIAS 0x3f
#define FLASH_FADE_FIRST_STEP 5
#define FLASH_FADE_BIAS_PER_STEP 0xa

/* 00028610.  Spell 11's full-screen cutscene, and the white flash that hands
   the screen back to the battle map.
 *
 * PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x50 and a bare RET, with nothing
 * read above [EBP]; the one call site at 000289b1 pushes nothing before the
 * CALL, cleans nothing after it and reads no answer, so this takes no argument
 * and returns none under the stack convention.
 *
 * THE FRAME CLOCK'S LATCH IS NOT INITIALISED, and that is behaviour rather
 * than an oversight.  [EBP-0x4] is never written before the first frame's wait
 * reads it at 00028788, so on that one tick the wait falls through on stack
 * garbage and the frame is presented without waiting.  Seeding it with 0 or
 * with the counter -- the obvious C -- costs an extra tick there and lengthens
 * the fade-in (rebuild_info/pitfalls.md).  data_fdps_timer_tick_counter is
 * written only by the timer interrupt and is declared volatile in gamedata.h,
 * without which wcc386 loads it once into a register and neither spin ever
 * ends.
 *
 * The two phases differ in three things and in nothing else.  The fade-in
 * refills the page's visible window from the frozen screen every tick, so the
 * animation comes up out of the picture the spell was cast over; the main
 * phase memsets the whole page instead, so the rest of the clip plays on
 * black.  The fade-in discards fdps_saf_advance_tick's answer, so its cursor
 * wraps rather than stopping; the main phase keeps it as its exit condition,
 * and mode 0 reports 1 once as it wraps, so that phase runs the clip to its
 * end exactly once.  And the blend level moves only in the fade-in: the main
 * phase inherits the last value the fade-in left, which is 0.
 *
 * THE CURSOR IS NOT RESET BETWEEN THEM.  Mag11.saf holds 22 frames and every
 * one of them dwells a single tick, so the 31-tick fade-in plays frames 0
 * through 21, wraps, plays 0 through 8 again and leaves the cursor on frame 9;
 * the main phase then picks the clip up there and runs frames 9 through 21, 13
 * ticks.  Resetting the cursor, shortening the fade-in to the clip's length,
 * or letting the fade-in stop at the end of the clip each changes what the
 * player sees (rebuild_info/pitfalls.md).
 *
 * The flag handed to fdps_draw_composite_sprite is 0 in both phases, so the
 * two sound effects Mag11.saf carries -- frame 0's sound 0 and frame 15's
 * sound 1 -- are never played; the audio for this spell is started by the
 * caller.  Every CALL in the body returns void or has its answer discarded
 * except fdps_vfs_load_entry's, the two malloc's, inp's inside the four spins
 * and the fdps_saf_advance_tick at 000287e5 whose EAX is stored into the
 * loop's flag at 000287ed. */
void fdps_play_spell_11_cutscene(void)
{
    /* The nine-dword draw request sprite.h describes.  Built once and then
       rewritten in place: only its item index moves after the first frame. */
    int request[DRAW_REQUEST_DWORDS];
    /* The three-dword mode-9 descriptor rleblend.h describes.  Its two table
       pointers are filled before anything else and never move again; only the
       level changes, once per fade-in tick. */
    int blend_descriptor[BLEND_DESC_DWORDS];
    /* The three-dword playback cursor saf.h describes.  Its image slot is
       filled by hand and the reset call zeroes the other two. */
    int playback_cursor[SAF_CURSOR_DWORDS];
    /* Mag11.saf.  This function owns it and frees it. */
    void *clip;
    /* The 368 x 248 page every frame is composed on, so that nothing is seen
       half-drawn.  The assembly keeps it only in the request's element 0 and
       re-reads it from there; the name is here for the reader. */
    unsigned char *work_page;
    /* The 320 x 200 picture that was on the adapter when the call was made.
       Freed at the end of the fade-in, which is the last thing that reads
       it. */
    unsigned char *screen_snapshot;
    /* The tick the previous frame ended on.  Deliberately not initialised --
       see the note above. */
    unsigned int last_tick;
    /* fdps_saf_advance_tick's answer in the main phase: 0 while the clip is
       still running, 1 on the tick that steps past its last frame. */
    int clip_ended;
    /* The fade-in's counter first, 0x1f down to 1, and then the white flash's,
       5 down to 0.  One slot in the frame serves both, [EBP-0x10]. */
    int fade_step;

    clip_ended = 0;
    clip = fdps_vfs_load_entry(SPELL11_ARCHIVE, SPELL11_CLIP);
    blend_descriptor[BLEND_DESC_SHADE_RAMP] =
        (int) data_fdps_palette_shade_ramp_table;
    blend_descriptor[BLEND_DESC_CUBE] = (int) data_fdps_inverse_palette_cube;

    work_page = (unsigned char *) malloc((size_t) SPELL11_PAGE_BYTES);
    request[DRAW_REQUEST_DEST_BASE] = (int) work_page;
    request[DRAW_REQUEST_DEST_PITCH] = SPELL11_PAGE_PITCH;
    request[DRAW_REQUEST_DEST_ROWS] = SPELL11_PAGE_ROWS;
    request[DRAW_REQUEST_BLIT_OPERAND] = (int) blend_descriptor;
    request[DRAW_REQUEST_X] = SPELL11_PAGE_MARGIN;
    request[DRAW_REQUEST_Y] = SPELL11_PAGE_MARGIN;
    request[DRAW_REQUEST_IMAGE] = (int) clip;

    playback_cursor[SAF_CURSOR_IMAGE] = (int) clip;
    fdps_saf_advance_tick(playback_cursor, 1);

    screen_snapshot = (unsigned char *) malloc((size_t) VGA_SCREEN_BYTES);
    memmove(screen_snapshot, (void *) VGA_SCREEN_BASE,
            (size_t) VGA_SCREEN_BYTES);

    request[DRAW_REQUEST_BLIT_MODE] = BLIT_MODE_TRANSLUCENT;

    for (fade_step = SPELL11_FADE_TICKS; fade_step > 0; fade_step--) {
        /* 15 down to 0, two ticks to a level.  Level 0 draws the source
           opaque and level 16 makes it invisible (rleblend.h), so the clip
           comes up out of the frozen screen rather than dissolving into it. */
        blend_descriptor[BLEND_DESC_LEVEL] =
            fade_step / SPELL11_FADE_LEVEL_DIVISOR;

        fdps_blit_rect((unsigned int) screen_snapshot, VGA_SCREEN_PITCH,
                       work_page + SPELL11_PAGE_WINDOW_AT, SPELL11_PAGE_PITCH,
                       VGA_SCREEN_PITCH, VGA_SCREEN_ROWS);
        request[DRAW_REQUEST_ITEM_INDEX] =
            playback_cursor[SAF_CURSOR_FRAME_INDEX];
        fdps_draw_composite_sprite(request, 0);
        fdps_saf_advance_tick(playback_cursor, 0);

        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* Spin until the retrace begins. */
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
            /* And until it ends, so the present runs inside the displayed
               part of the frame. */
        }

        fdps_blit_rect((unsigned int) (work_page + SPELL11_PAGE_WINDOW_AT),
                       SPELL11_PAGE_PITCH, (void *) VGA_SCREEN_BASE,
                       VGA_SCREEN_PITCH, VGA_SCREEN_PITCH, VGA_SCREEN_ROWS);

        while (last_tick == data_fdps_timer_tick_counter) {
            /* spin: only the timer interrupt can end this */
        }
        last_tick = data_fdps_timer_tick_counter;
    }
    free(screen_snapshot);

    while (clip_ended == 0) {
        request[DRAW_REQUEST_ITEM_INDEX] =
            playback_cursor[SAF_CURSOR_FRAME_INDEX];
        memset(work_page, 0, (size_t) SPELL11_PAGE_BYTES);
        fdps_draw_composite_sprite(request, 0);
        clip_ended = fdps_saf_advance_tick(playback_cursor, 0);

        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* The same pair of spins, for the same reason. */
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
        }

        fdps_blit_rect((unsigned int) (work_page + SPELL11_PAGE_WINDOW_AT),
                       SPELL11_PAGE_PITCH, (void *) VGA_SCREEN_BASE,
                       VGA_SCREEN_PITCH, VGA_SCREEN_PITCH, VGA_SCREEN_ROWS);

        while (last_tick == data_fdps_timer_tick_counter) {
            /* spin: only the timer interrupt can end this */
        }
        last_tick = data_fdps_timer_tick_counter;
    }

    free(clip);
    free(work_page);

    /* The maximum per-channel bias clamps every DAC entry to white, and the
       aperture is blanked under it.  The six steps that follow walk the bias
       down an arithmetic progression the opening 0x3f is not part of, so the
       screen whites out and then fades into the live map at its true
       palette. */
    fdps_set_palette_range(
        (struct fdps_palette_entry *) data_fdps_vga_main_palette_ptr,
        FADE_FIRST_DAC_ENTRY, FADE_LAST_DAC_ENTRY,
        WHITE_FLASH_BIAS, WHITE_FLASH_BIAS, WHITE_FLASH_BIAS);
    memset((void *) VGA_SCREEN_BASE, 0, (size_t) VGA_SCREEN_BYTES);

    for (fade_step = FLASH_FADE_FIRST_STEP; fade_step >= 0; fade_step--) {
        fdps_set_palette_range(
            (struct fdps_palette_entry *) data_fdps_vga_main_palette_ptr,
            FADE_FIRST_DAC_ENTRY, FADE_LAST_DAC_ENTRY,
            fade_step * FLASH_FADE_BIAS_PER_STEP,
            fade_step * FLASH_FADE_BIAS_PER_STEP,
            fade_step * FLASH_FADE_BIAS_PER_STEP);
        fdps_render_view_frame();
    }
}

/* The DAC write port pair: the entry number goes to 0x3c8 and the entry's red,
   green and blue components follow on 0x3c9, six bits each.  Both are port
   numbers rather than addresses the linker places, so they stay literals. */
#define VGA_DAC_WRITE_INDEX 0x3c8
#define VGA_DAC_DATA 0x3c9

/* The flash touches one DAC entry and no other -- PUSH 0x0 / PUSH 0x3c8 at
   00029153 -- and runs four times: MOV dword ptr [EBP-0x4],0x0 at 00029139
   with CMP against 0x4 / JL at 00029140. */
#define SPELL_FLASH_DAC_ENTRY 0
#define SPELL_FLASH_PASSES 4

/* One colour per MAGICDAT.DAT record, and the file has 0x28 of them.  The three
   read-only blocks the planes are copied from are 0x28 bytes apart -- 00027674,
   0002769c, 000276c4 -- so the count is the blocks' own and not a guess. */
#define SPELL_FLASH_COLOR_COUNT 40

/* 00029100.  Four passes of one spell's signature colour against black, with a
 * whole presented frame of the battle view held in each.
 *
 * PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x7c and a bare RET.  The one
 * argument is read from [EBP+0x14], which is the first stack slot above the
 * return address and the four saved registers, and the single call site at
 * 0002891c inside fdps_cast_spell_on_targets pushes it and cleans the 4 bytes
 * off itself.  So this takes one stack argument, returns nothing, and is the
 * stack convention like the rest of the game code.
 *
 * The three planes are function-local arrays with initialisers, which is what
 * the three REP MOVSDs at 00029119, 00029128 and 00029137 are: ten dwords each
 * copied onto the frame out of read-only storage before anything else happens,
 * in the declaration order below.  Their contents are those three blocks read
 * back byte for byte.  They are not one table of triples: the original keeps a
 * whole plane per channel, and the three loads inside the loop reach three
 * separate frame slots 0x28 apart.
 *
 * The index is the spell id itself, with no bias and no bound test.  MOV EAX,
 * [EBP+0x14] / MOV AL,byte ptr [EAX + EBP*0x1 + -0x7c] at 00029162 makes the
 * whole dword the index, and the AND EAX,0xff that follows each load is what
 * makes a plane unsigned char: a signed one would have been sign extended into
 * the argument outp is handed.
 *
 * The stored levels are 6-bit DAC components and they follow the spell's
 * element: (63,0,0) for 業火, 狂暴巨燄, 烈獄之火, 震空重力彈, 神之祝福, 流星箭
 * and 靈彈超必殺 -- ids 0x00-0x02, 0x0c, 0x14, 0x1e and 0x1f -- (57,42,25) for
 * the two ground shocks 裂地術 and 封神裂震, (0,63,0) for 甦癒術, (63,9,0) for
 * 審判之雷, and full white for every other id.
 *
 * DAC ENTRY 0 IS LEFT BLACK, not put back.  Nothing here reads the entry before
 * writing it and the last pass ends on the black write, so the caller inherits
 * a palette whose index 0 is black whatever it held before the call.  A body
 * that saved and restored it -- the obvious courtesy, and what a reader expects
 * of a routine that borrows a palette entry -- changes what the map is drawn
 * over once the cast is finished.
 *
 * The pacing is not this function's.  Each fdps_render_view_frame holds until
 * the timer's tick counter moves (mapdraw.h), so the eight presented frames are
 * eight ticks whatever the machine's speed, and the instruction count of the
 * loop has nothing to do with how long a flash lasts.
 *
 * Neither fdps_render_view_frame nor outp returns anything this reads: every
 * CALL in the body is followed by ADD ESP,0x8 or by nothing at all, and EAX is
 * reloaded from the argument slot before each of the three plane loads. */
void fdps_play_spell_palette_flash(int spell_id)
{
    /* 00027674.  The red component of each spell's flash colour. */
    unsigned char flash_red[SPELL_FLASH_COLOR_COUNT] = {
        63, 63, 63, 63, 63, 63, 63, 63,
        63, 63, 57, 57, 63, 63, 63, 63,
        63, 63, 63, 63, 63, 63, 63, 63,
         0, 63, 63, 63, 63, 63, 63, 63,
        63, 63, 63, 63, 63, 63, 63, 63
    };
    /* 0002769c.  The green component. */
    unsigned char flash_green[SPELL_FLASH_COLOR_COUNT] = {
         0,  0,  0, 63, 63, 63, 63, 63,
        63, 63, 42, 42,  0, 63, 63, 63,
        63, 63, 63, 63,  0, 63, 63, 63,
        63, 63, 63, 63, 63, 63,  0,  0,
         9, 63, 63, 63, 63, 63, 63, 63
    };
    /* 000276c4.  The blue component. */
    unsigned char flash_blue[SPELL_FLASH_COLOR_COUNT] = {
         0,  0,  0, 63, 63, 63, 63, 63,
        63, 63, 25, 25,  0, 63, 63, 63,
        63, 63, 63, 63,  0, 63, 63, 63,
         0, 63, 63, 63, 63, 63,  0,  0,
         0, 63, 63, 63, 63, 63, 63, 63
    };
    /* Which of the four strobes is being played. */
    int pass;

    for (pass = 0; pass < SPELL_FLASH_PASSES; pass++) {
        outp(VGA_DAC_WRITE_INDEX, SPELL_FLASH_DAC_ENTRY);
        outp(VGA_DAC_DATA, flash_red[spell_id]);
        outp(VGA_DAC_DATA, flash_green[spell_id]);
        outp(VGA_DAC_DATA, flash_blue[spell_id]);
        fdps_render_view_frame();

        outp(VGA_DAC_WRITE_INDEX, SPELL_FLASH_DAC_ENTRY);
        outp(VGA_DAC_DATA, 0);
        outp(VGA_DAC_DATA, 0);
        outp(VGA_DAC_DATA, 0);
        fdps_render_view_frame();
    }
}
