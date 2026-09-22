/* unitatk.c -- what a unit does with its own turn on the battle map: the
 * physical attack, and the rest.
 *
 * fdps_unit_attack_target is what the map calls for the attack.  It decides
 * how many blows the swing lands, plays each blow's animation, hands the
 * numbers to the resolver and drains the target's HP bar down to what the blow
 * left.  fdps_unit_resolve_attack_hit is that resolver: one blow worked out
 * from the two units' records, the ground they stand on and the weapon's hit
 * effect.  fdps_unit_rest is the other command a unit can spend its turn on:
 * one white flash of the unit and a fifth of its maximum HP back.
 *
 * See unitatk.h for what a caller has to know.  The only state this file owns
 * is data_fdps_battle_last_hit_or_miss_flag; everything else it touches is
 * either one of the two unit records or a global gamedata.h declares.
 *
 * rand and malloc come from <stdlib.h>, delay from <i86.h> and inp from
 * <conio.h>, which is where Watcom 10.0a declares each of them; all of them
 * are real calls in the original -- CALL 00042cf8, CALL 0003d370, CALL
 * 0003d375 and CALL 0003d4e4 -- and not inline expansions.
 */
#include <stdlib.h>
#include <i86.h>
#include <conio.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "unit.h"
#include "unititem.h"
#include "table.h"
#include "maptile.h"
#include "palette.h"
#include "anim.h"
#include "gauge.h"
#include "mapcur.h"
#include "mapdraw.h"
#include "sprite.h"
#include "blit.h"
#include "audio.h"
#include "unitatk.h"

/* Global data owned by this file, in the original image's address order.
 * Initialised definitions come first and their order is the layout
 * (rebuild_info/data_emit.md); zero-filled ones follow. */

/* 00064010. Starts zero in the image; fdps_unit_resolve_attack_hit sets it to
   1 on entry and clears it on a miss, so the initial value is never observed.
   */
unsigned char data_fdps_battle_last_hit_or_miss_flag;

/* End of global data. */

/* The weapon hit effects, read out of item record byte +9.  Both functions in
   this file read that byte and each answers a different part of it:
   fdps_unit_resolve_attack_hit's chain compares against 3, 4 and 1 and passes
   over 2, while fdps_unit_attack_target compares against 2 alone and answers
   it by calling the resolver twice.  So each blow of a double-strike weapon
   resolves as an ordinary one. */
#define WEAPON_EFFECT_PARALYSIS 1
#define WEAPON_EFFECT_DOUBLE_STRIKE 2
#define WEAPON_EFFECT_CRITICAL 3
#define WEAPON_EFFECT_POISON 4

/* Class 0x19, 機兵, is immune to both status effects.  The test is against
   this one code and not against a property of the class record, so a class
   that is mechanical in every other respect is still poisonable. */
#define CLASS_IMMUNE_TO_STATUS 0x19

/* The two status timers this function writes, as indices into struct
   fdps_unit_record's status_timers[]: [3] is record offset 0x25 and [4] is
   0x26.  Each is a turn count and each is ASSIGNED, so a fresh roll replaces
   whatever the target had left rather than extending it. */
#define POISON_TIMER_SLOT 3
#define PARALYSIS_TIMER_SLOT 4

/* rand() % 4 + 2 and rand() % 2 + 2: poison runs two to five turns and
   paralysis two or three. */
#define POISON_TURNS_SPREAD 4
#define POISON_TURNS_BASE 2
#define PARALYSIS_TURNS_SPREAD 2
#define PARALYSIS_TURNS_BASE 2

/* Both screen flashes rewrite the whole DAC out of the master palette, entries
   0 to 255 inclusive, and differ only in the three biases. */
#define FLASH_FIRST_DAC_ENTRY 0
#define FLASH_LAST_DAC_ENTRY 0xff

/* The status flash is green: the red and blue channels are pulled 64 below the
   master palette while green is pushed 64 above it.  The critical flash is
   white, 63 on all three. */
#define STATUS_FLASH_RED_BIAS (-0x40)
#define STATUS_FLASH_GREEN_BIAS 0x40
#define STATUS_FLASH_BLUE_BIAS (-0x40)
#define CRITICAL_FLASH_BIAS 0x3f

/* Bias 0 uploads the master palette untouched, which is what puts the normal
   screen back between the two beats of a flash and at the end of one. */
#define FLASH_NO_BIAS 0

/* How long each beat of a flash is held, in milliseconds: the coloured frame
   for 20 and the normal frame for 40. */
#define FLASH_LIT_MS 0x14
#define FLASH_DARK_MS 0x28

/* Every roll in here is a percentage: rand() % 100 against a rate. */
#define PERCENT 100

/* Damage is nine tenths of the stat difference, and the random bonus on top is
   rand() % (damage / 9) -- a ninth of the damage, so the bonus is at most
   another ninth and is skipped entirely while the damage is below nine. */
#define DAMAGE_NUMERATOR 9
#define DAMAGE_DENOMINATOR 10
#define DAMAGE_SPREAD_DIVISOR 9

/* Only a blow struck by a unit on the player's side pays experience: struct
   fdps_unit_record's side byte compared against 2. */
#define PLAYER_SIDE 2

/* Portrait ids from 0x3c up are the enemy roster, and the enemy record index
   is the portrait id less that base.  Below it the target is a player-side or
   guest character and the blow pays nothing. */
#define FIRST_ENEMY_PORTRAIT_ID 0x3c

/* Portrait ids above 10 are 蘭斯洛特 and every guest or NPC unit, as opposed
   to the eleven permanent roster characters at 0..10.  Such an attacker has 30
   added to the level it divides the award by, which is what keeps a guest from
   farming experience the party keeps. */
#define LAST_PERMANENT_ROSTER_PORTRAIT_ID 10
#define GUEST_LEVEL_PENALTY 0x1e

/* Which of the unit gauge sheet's three graphics fills the target's bar, from
   CMP byte ptr [EAX+0x6],0x0 / JNZ at 0001c3c5: a side byte of 0 picks graphic
   2 and every other side picks graphic 1, the same reading
   fdps_play_attack_animation (anim.c) and fdps_battle_show_combat_gauges
   (gauge.c) make of the same byte. */
#define GAUGE_SIDE_ZERO_GRAPHIC 2
#define GAUGE_OTHER_SIDE_GRAPHIC 1

/* The bar's 41-column interior, the span the HP proportion is taken over:
   IMUL EAX,dword ptr [EBP-0x24],0x29 at 0001c466 and again at 0001c49e.  It is
   gauge.c's UNIT_GAUGE_INTERIOR_WIDTH and the ceiling is the same
   (value * 0x29 + max - 1) / max, but it is written out here rather than
   called: the shipped image has no CALL to fdps_draw_unit_gauge_proportional
   at either place (rebuild_info/build_flags.md). */
#define UNIT_GAUGE_INTERIOR_WIDTH 0x29

/* Where the drain paints and how.  All of it is hard-coded in the original --
   LEA EDX,[EAX + 0xa0000] at 0001c4db with PUSH 0x140 at 0001c4c7, ADD
   EAX,0x4 at 0001c4d2 and ADD EDX,0x4 at 0001c4e6 for the two insets, and the
   two PUSH 0x0 at 0001c4bb -- and 0xa0000 stays a literal because it is where
   the display adapter answers, not the address of anything the linker places
   (rebuild_info/pitfalls.md, contract E).  The bar goes down four pixels right
   and four pixels below the position the caller hands over, which is the same
   four-pixel inset the battle window is blitted to.  Mode 0 is
   fdps_draw_unit_gauge's plain painter, which never reads the strength, so the
   alpha of 0 is carried and not used (gauge.h). */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140
#define GAUGE_SCREEN_INSET 4
#define GAUGE_DRAIN_MODE_PLAIN 0
#define GAUGE_DRAIN_ALPHA 0

/* PUSH 0x8 / CALL delay at 0001c4f2: the bar gives up one pixel every 8 ms. */
#define GAUGE_DRAIN_STEP_MS 8

/* CMP EDX,0x3 / JGE at 0001c42c, on the remainder of a signed IDIV by 100: a
   flat 3 percent chance of a second strike on every attack, whatever weapon
   the attacker holds. */
#define BONUS_STRIKE_PERCENT 3

/* MOV dword ptr [EBP-0x14],0x1 at 0001c3ac and the two MOV ...,0x2 at 0001c431
   and 0001c43e.  Neither test adds to the count; both set it, so an attack
   that passes both still lands two blows and not three. */
#define STRIKES_ORDINARY 1
#define STRIKES_DOUBLE 2

/* 0001c3a0.  The frame is the plain -4s one: PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP, SUB ESP,0x2c, the three arguments read from [EBP+0x14], [EBP+0x18]
   and [EBP+0x1c], RET with no immediate, and both call sites in its one caller
   -- 00012f3e and 00012fbe in fdps_map_actor_move_and_attack -- pushing three
   dwords and doing ADD ESP,0xc afterwards.

   THE rand CALL IS UNCONDITIONAL AND THE TWO TESTS ARE SEPARATE.  CALL rand at
   0001c419 stands above both of them, and the double-strike test at 0001c438
   is a second `if` and not the other arm of the first.  Folding them into
   `if (hit_effect == 2 || rand() % 100 < 3)` short-circuits the call away
   whenever a double-strike weapon is equipped and shifts the whole
   pseudo-random stream from that attack onward, changing every later hit,
   critical and status roll (rebuild_info/pitfalls.md).

   THE DOUBLE-STRIKE BRANCH NEVER READS THE WEAPON'S RATE.  Item byte +0x0a,
   the percentage the resolver rolls for a poison or a paralysis weapon, is not
   loaded anywhere in this function, so a double-strike weapon always gets both
   blows.

   THE HP AND THE MAXIMUM ARE RE-READ FROM THE RECORD AT THE TOP OF EVERY
   STRIKE, at 0001c455 and 0001c45f, which is inside the loop and not above it.
   That matters for the second blow: the resolver has written the first blow's
   HP back into the record, so the second bar starts where the first one
   stopped.

   THE COUNT-DOWN TEST IS >= AND NOT >.  CMP EAX,[EBP-0x18] / JL at 0001c4b9
   leaves the loop only when the width has fallen BELOW the new one, so a blow
   that takes nothing off still repaints the bar once at its unchanged width.

   NEITHER THE UNIT INDICES NOR THE WEAPON LOOKUP IS GUARDED, and neither is
   the division by the maximum HP.  fdps_unit_find_equipped_slot answers -1 for
   an attacker with nothing equipped and that -1 goes straight into
   fdps_unit_get_item_id, which then reads unit record byte +9 and asks the
   item table for whatever id it holds (unititem.h); a target whose maximum HP
   is 0 is a divide error here exactly as it is in the original.

   FOUR CALLS' ANSWERS ARE READ.  fdps_get_unit_record's pointer at 0001c3bf is
   the record every field below comes off and stays live to the end of the
   loop; fdps_unit_find_equipped_slot's slot at 0001c3e9 is
   fdps_unit_get_item_id's second argument; that id at 0001c3fc is
   fdps_get_item_record's argument; and fdps_unit_resolve_attack_hit's answer
   at 0001c49b is the new HP, which is the second bar width, the loop's own
   exit test and the function's return value.  fdps_play_attack_animation,
   fdps_draw_unit_gauge and delay return nothing the original reads. */
int fdps_unit_attack_target(int attacker_unit_index, int target_unit_index,
                            int *gauge_pos)
{
    /* The target's record, resolved once and read on every strike. */
    struct fdps_unit_record *target;
    /* The attacker's equipped weapon, read only for its hit effect. */
    struct fdps_item_effect *weapon;
    /* Which graphic of the sheet the filled part of the bar is drawn from. */
    int gauge_gfx_index;
    /* The inventory slot the weapon sits in, and the item id that slot holds.
       The original keeps both in the one stack slot at [EBP-0xc], the second
       overwriting the first; they are two locals here because they hold two
       different things and neither name is true of the other value. */
    int weapon_slot;
    int weapon_item_id;
    /* Item record byte +9, widened unsigned by the XOR EAX,EAX / MOV AL at
       0001c40e. */
    int hit_effect;
    /* How many blows this attack still owes, counted down to -1. */
    int strike_count;
    /* The target's HP: read off the record at the top of each strike and then
       replaced by what the resolver hands back.  One local, because the
       original keeps both in [EBP-0x24]. */
    int current_hp;
    /* The target's maximum HP, what the bar is a proportion of. */
    int max_hp;
    /* The bar width being painted this step.  It starts at the width the HP
       had before the blow and walks down one pixel at a time. */
    int bar_width;
    /* Where that walk stops: the width the HP left by the blow gives. */
    int bar_width_after;

    strike_count = STRIKES_ORDINARY;

    target = fdps_get_unit_record(target_unit_index);
    if (target->side == 0) {
        gauge_gfx_index = GAUGE_SIDE_ZERO_GRAPHIC;
    } else {
        gauge_gfx_index = GAUGE_OTHER_SIDE_GRAPHIC;
    }

    weapon_slot = fdps_unit_find_equipped_slot(attacker_unit_index, 0);
    weapon_item_id = fdps_unit_get_item_id(attacker_unit_index, weapon_slot);
    weapon = fdps_get_item_record(weapon_item_id);
    hit_effect = weapon->hit_effect;

    if (rand() % PERCENT < BONUS_STRIKE_PERCENT) {
        strike_count = STRIKES_DOUBLE;
    }
    if (hit_effect == WEAPON_EFFECT_DOUBLE_STRIKE) {
        strike_count = STRIKES_DOUBLE;
    }

    /* The strike loop.  The original decrements at the top and leaves on -1 --
       DEC dword ptr [EBP-0x14] / CMP ...,-0x1 / JZ at 0001c445 -- so a count of
       1 makes one pass and a count of 2 makes two, and the bottom test at
       0001c504 is a JNZ back to that decrement, which is what stops a second
       blow falling on a target the first one killed. */
    do {
        strike_count--;
        if (strike_count == -1) {
            break;
        }

        current_hp = target->hp_current;
        max_hp = target->hp_max;
        bar_width = (current_hp * UNIT_GAUGE_INTERIOR_WIDTH + max_hp - 1)
                    / max_hp;

        fdps_play_attack_animation(attacker_unit_index, target_unit_index);
        current_hp = fdps_unit_resolve_attack_hit(attacker_unit_index,
                                                  target_unit_index);

        bar_width_after = (current_hp * UNIT_GAUGE_INTERIOR_WIDTH + max_hp - 1)
                          / max_hp;

        while (bar_width >= bar_width_after) {
            fdps_draw_unit_gauge(
                (unsigned char *)
                    (VGA_SCREEN_BASE
                     + (gauge_pos[1] + GAUGE_SCREEN_INSET) * VGA_SCREEN_PITCH
                     + gauge_pos[0] + GAUGE_SCREEN_INSET),
                VGA_SCREEN_PITCH, gauge_gfx_index, bar_width,
                GAUGE_DRAIN_MODE_PLAIN, GAUGE_DRAIN_ALPHA);
            delay((unsigned int) GAUGE_DRAIN_STEP_MS);
            bar_width--;
        }
    } while (current_hp != 0);

    return current_hp;
}

/* 0001c520.  The frame is the plain -4s one: PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP, SUB ESP,0x54, with the two arguments read from [EBP+0x14] and
   [EBP+0x18] and the caller doing ADD ESP,0x8 after the CALL at 0001c493.

   The order of the four rolls is the whole shape of the function and is not
   the order a reader would guess.  The weapon's status effect is rolled and
   applied BEFORE the accuracy roll and is not inside it -- 0001c6de through
   0001c8ae all sit above the CALL to rand at 0001c8b1 -- so a poison or
   paralysis weapon lands its ailment on a blow that then misses.  Moving the
   two status blocks into the hit branch, which is where they read as though
   they belong, changes the game (rebuild_info/pitfalls.md).

   The terrain scaling comes before all of that, and each combatant is scaled
   only if fdps_unit_is_flying says it is on the ground, so a flying unit
   neither gains nor loses from the tile it is drawn over.

   Every division here is the signed IDIV and every operand reaches it sign
   extended (SAR EDX,0x1f), so all of them truncate towards zero.  That is
   load-bearing in one place: a stat difference below zero gives a damage of
   -13 rather than -14 for (5 - 20) * 9 / 10, and it is then clamped to 0
   anyway -- but the clamp is a separate test and not a max(), and removing
   either half changes what a stronger defender does to the blow.

   Three of the arithmetic steps have no guard and the original has none
   either: the experience divides by the attacker's level byte and then by the
   target's max HP, and the random damage bonus divides by a ninth of the
   damage.  The last is guarded by the != 0 test at 0001c9c7; the other two are
   not, so a level-0 attacker or a target with no maximum HP is a divide
   error in the original as much as here.

   The two green flashes are written out twice rather than shared, and the
   white one is not folded in with them.  They are not the same sequence: the
   green one closes with a fourth upload AND a 40 ms delay, while the white one
   ends on the fourth upload and goes straight to the damage (there is no CALL
   to delay between 0001c977 and 0001c97f).  One shared helper would put a 40
   ms pause in the middle of every critical the original does not have
   (rebuild_info/pitfalls.md). */
int fdps_unit_resolve_attack_hit(int attacker_unit_index,
                                 int target_unit_index)
{
    struct fdps_unit_record *attacker;
    struct fdps_unit_record *target;
    struct fdps_class_record *attacker_class;
    struct fdps_item_effect *weapon;
    struct fdps_enemy_data *enemy;
    int class_record_index;
    int weapon_slot;
    int weapon_item_id;
    int hit_effect;
    int hit_effect_rate;
    int critical_rate;
    int attack_power;
    int defense_power;
    int accuracy;
    int evasion;
    int current_hp;
    int max_hp;
    int damage;
    int damage_spread;
    unsigned char attacker_level;
    unsigned char target_level;

    damage = 0;

    /* MOV dword ptr [EBP-0x24],0x3 at 0001c533, before anything else.  Every
       path through the function reaches the unconditional overwrite from the
       class record below, so the 3 never survives to a read; it is kept
       because it is the store the original makes. */
    critical_rate = 3;

    data_fdps_battle_last_hit_or_miss_flag = 1;

    attacker = fdps_get_unit_record(attacker_unit_index);
    target = fdps_get_unit_record(target_unit_index);

    /* Six MOVSX word loads: the stats are signed 16-bit fields widened into
       signed 32-bit locals, and every later compare on them is a signed jump.
       The attacker contributes attack and accuracy, the target defense,
       evasion and both HP figures. */
    attack_power = attacker->ap;
    defense_power = target->dp;
    current_hp = target->hp_current;
    max_hp = target->hp_max;
    accuracy = attacker->hit;
    evasion = target->ev;

    /* The critical rate is a property of the class and comes from PROMAP.DAT
       row clazz + 1, byte +8.  The +1 is the row offset the whole game uses
       for this table and is not a fencepost of this function's own. */
    class_record_index = attacker->clazz + 1;
    attacker_class = fdps_get_class_record(class_record_index);

    /* Both levels are copied out as BYTES and stay bytes.  The attacker's is
       what the guest penalty is added to, with ADD byte ptr [EBP-0x8],0x1e at
       0001ca48, so the sum wraps at 256 rather than widening -- a level 230
       guest divides by 4 and not by 260. */
    attacker_level = attacker->level;
    target_level = target->level;

    /* The equipped weapon, through the same three calls the caller makes.
       Nothing here checks the slot: a unit with no equipped weapon gets -1
       back and fdps_unit_get_item_id then reads the byte in front of the
       inventory, which is record offset 0x09, and asks the item table for
       whatever id that holds. */
    weapon_slot = fdps_unit_find_equipped_slot(attacker_unit_index, 0);
    weapon_item_id = fdps_unit_get_item_id(attacker_unit_index, weapon_slot);
    weapon = fdps_get_item_record(weapon_item_id);
    hit_effect = weapon->hit_effect;
    hit_effect_rate = weapon->hit_effect_rate;

    /* Terrain, attacker side.  fdps_map_load_tile_info leaves the tile's
       terrain class in data_fdps_map_tile_terrain_type and the table entry it
       selects is a signed percentage of the stat, added to it. */
    if (fdps_unit_is_flying(attacker_unit_index) == 0) {
        fdps_map_load_tile_info(attacker->pos_x, attacker->pos_y);
        attack_power +=
            data_fdps_battle_tile_attr_ap_modifier_table
                [data_fdps_map_tile_terrain_type] * attack_power / PERCENT;
    }

    /* Terrain, target side.  Same shape, the other table and the other stat,
       and the tile is the target's own -- the call reloads the scratch block,
       so the attacker's terrain is gone by the time this runs. */
    if (fdps_unit_is_flying(target_unit_index) == 0) {
        fdps_map_load_tile_info(target->pos_x, target->pos_y);
        defense_power +=
            data_fdps_battle_tile_attr_def_modifier_table
                [data_fdps_map_tile_terrain_type] * defense_power / PERCENT;
    }

    critical_rate = attacker_class->critical;

    if (hit_effect == WEAPON_EFFECT_CRITICAL) {
        /* A critical weapon does not roll here; its percentage is simply added
           to the class rate and the single roll below decides. */
        critical_rate += hit_effect_rate;
    } else if (hit_effect == WEAPON_EFFECT_POISON) {
        if (rand() % PERCENT < hit_effect_rate &&
            target->clazz != CLASS_IMMUNE_TO_STATUS) {
            target->status_timers[POISON_TIMER_SLOT] =
                (unsigned char) (rand() % POISON_TURNS_SPREAD +
                                 POISON_TURNS_BASE);

            fdps_set_palette_range(
                (struct fdps_palette_entry *) data_fdps_vga_main_palette_ptr,
                FLASH_FIRST_DAC_ENTRY, FLASH_LAST_DAC_ENTRY,
                STATUS_FLASH_RED_BIAS, STATUS_FLASH_GREEN_BIAS,
                STATUS_FLASH_BLUE_BIAS);
            delay((unsigned int) FLASH_LIT_MS);
            fdps_set_palette_range(
                (struct fdps_palette_entry *) data_fdps_vga_main_palette_ptr,
                FLASH_FIRST_DAC_ENTRY, FLASH_LAST_DAC_ENTRY,
                FLASH_NO_BIAS, FLASH_NO_BIAS, FLASH_NO_BIAS);
            delay((unsigned int) FLASH_DARK_MS);
            fdps_set_palette_range(
                (struct fdps_palette_entry *) data_fdps_vga_main_palette_ptr,
                FLASH_FIRST_DAC_ENTRY, FLASH_LAST_DAC_ENTRY,
                STATUS_FLASH_RED_BIAS, STATUS_FLASH_GREEN_BIAS,
                STATUS_FLASH_BLUE_BIAS);
            delay((unsigned int) FLASH_LIT_MS);
            fdps_set_palette_range(
                (struct fdps_palette_entry *) data_fdps_vga_main_palette_ptr,
                FLASH_FIRST_DAC_ENTRY, FLASH_LAST_DAC_ENTRY,
                FLASH_NO_BIAS, FLASH_NO_BIAS, FLASH_NO_BIAS);
            delay((unsigned int) FLASH_DARK_MS);
        }
    } else if (hit_effect == WEAPON_EFFECT_PARALYSIS) {
        if (rand() % PERCENT < hit_effect_rate &&
            target->clazz != CLASS_IMMUNE_TO_STATUS) {
            target->status_timers[PARALYSIS_TIMER_SLOT] =
                (unsigned char) (rand() % PARALYSIS_TURNS_SPREAD +
                                 PARALYSIS_TURNS_BASE);

            fdps_set_palette_range(
                (struct fdps_palette_entry *) data_fdps_vga_main_palette_ptr,
                FLASH_FIRST_DAC_ENTRY, FLASH_LAST_DAC_ENTRY,
                STATUS_FLASH_RED_BIAS, STATUS_FLASH_GREEN_BIAS,
                STATUS_FLASH_BLUE_BIAS);
            delay((unsigned int) FLASH_LIT_MS);
            fdps_set_palette_range(
                (struct fdps_palette_entry *) data_fdps_vga_main_palette_ptr,
                FLASH_FIRST_DAC_ENTRY, FLASH_LAST_DAC_ENTRY,
                FLASH_NO_BIAS, FLASH_NO_BIAS, FLASH_NO_BIAS);
            delay((unsigned int) FLASH_DARK_MS);
            fdps_set_palette_range(
                (struct fdps_palette_entry *) data_fdps_vga_main_palette_ptr,
                FLASH_FIRST_DAC_ENTRY, FLASH_LAST_DAC_ENTRY,
                STATUS_FLASH_RED_BIAS, STATUS_FLASH_GREEN_BIAS,
                STATUS_FLASH_BLUE_BIAS);
            delay((unsigned int) FLASH_LIT_MS);
            fdps_set_palette_range(
                (struct fdps_palette_entry *) data_fdps_vga_main_palette_ptr,
                FLASH_FIRST_DAC_ENTRY, FLASH_LAST_DAC_ENTRY,
                FLASH_NO_BIAS, FLASH_NO_BIAS, FLASH_NO_BIAS);
            delay((unsigned int) FLASH_DARK_MS);
        }
    }

    /* The accuracy roll.  accuracy - evasion is formed as a signed difference
       first and the remainder compared against it with JGE, so a difference of
       100 or more always lands and a difference of 0 or less never does. */
    if (rand() % PERCENT < accuracy - evasion) {
        data_fdps_battle_last_hit_or_miss_flag = 0;

        if (rand() % PERCENT < critical_rate) {
            fdps_set_palette_range(
                (struct fdps_palette_entry *) data_fdps_vga_main_palette_ptr,
                FLASH_FIRST_DAC_ENTRY, FLASH_LAST_DAC_ENTRY,
                CRITICAL_FLASH_BIAS, CRITICAL_FLASH_BIAS,
                CRITICAL_FLASH_BIAS);
            delay((unsigned int) FLASH_LIT_MS);
            fdps_set_palette_range(
                (struct fdps_palette_entry *) data_fdps_vga_main_palette_ptr,
                FLASH_FIRST_DAC_ENTRY, FLASH_LAST_DAC_ENTRY,
                FLASH_NO_BIAS, FLASH_NO_BIAS, FLASH_NO_BIAS);
            delay((unsigned int) FLASH_DARK_MS);
            fdps_set_palette_range(
                (struct fdps_palette_entry *) data_fdps_vga_main_palette_ptr,
                FLASH_FIRST_DAC_ENTRY, FLASH_LAST_DAC_ENTRY,
                CRITICAL_FLASH_BIAS, CRITICAL_FLASH_BIAS,
                CRITICAL_FLASH_BIAS);
            delay((unsigned int) FLASH_LIT_MS);
            fdps_set_palette_range(
                (struct fdps_palette_entry *) data_fdps_vga_main_palette_ptr,
                FLASH_FIRST_DAC_ENTRY, FLASH_LAST_DAC_ENTRY,
                FLASH_NO_BIAS, FLASH_NO_BIAS, FLASH_NO_BIAS);

            /* SAR-based halving of the terrain-modified defense, so it
               truncates towards zero.  A critical is worth exactly this and
               does not touch the attack side. */
            defense_power = defense_power / 2;
        }

        damage = (attack_power - defense_power) * DAMAGE_NUMERATOR /
                 DAMAGE_DENOMINATOR;
        if (damage < 0) {
            damage = 0;
        }

        damage_spread = damage / DAMAGE_SPREAD_DIVISOR;
        if (damage_spread != 0) {
            damage += rand() % damage_spread;
        }

        current_hp -= damage;
        if (current_hp < 0) {
            current_hp = 0;
        }
    }

    /* Written back on both paths: the store at 0001c9f2 is the target of the
       miss branch as well as the fall-through from the hit, so a miss rewrites
       the HP the target already had. */
    target->hp_current = (short) current_hp;

    if (attacker->side == PLAYER_SIDE &&
        target->portrait_id >= FIRST_ENEMY_PORTRAIT_ID) {
        enemy = fdps_get_enemy_record(target->portrait_id -
                                      FIRST_ENEMY_PORTRAIT_ID);

        if (attacker->portrait_id > LAST_PERMANENT_ROSTER_PORTRAIT_ID) {
            attacker_level = (unsigned char)
                             (attacker_level + GUEST_LEVEL_PENALTY);
        }

        /* Both levels and the reward reach the multiply and the divide as
           zero-extended bytes, so the arithmetic is on small positive
           integers however large the fields look. */
        data_fdps_battle_pending_xp_credit =
            target_level * enemy->exp_reward / attacker_level;

        /* A survivor pays only the fraction of itself the blow took off, so a
           kill is the only blow that pays the whole award.  The test is on the
           HP left, not on whether the blow landed: a miss leaves the HP
           non-zero and scales the award by a damage of 0. */
        if (current_hp != 0) {
            data_fdps_battle_pending_xp_credit =
                data_fdps_battle_pending_xp_credit * damage / max_hp;
        }
    }

    return current_hp;
}

/* ---- fdps_unit_rest, 000120d0 ------------------------------------------ */

/* The two map cursor overlay modes the rest moves between: 0 is the value
   fdps_draw_map_cursor's dispatch chain does not name, so no cursor is drawn
   at all, and 1 is the plain box the map sits at otherwise (gamedata.h). */
#define MAP_CURSOR_HIDDEN 0
#define MAP_CURSOR_PLAIN 1

/* The page the flash frame is composed on: a whole 360 by 240 8bpp surface at
   a 360-byte pitch, the 0x15180 pushed to malloc at 00012144 and the 0x168
   pushed as the source stride at 000121ac.  It is the surface
   fdps_draw_scene_layers and fdps_blit_unit_sprite both hardwire. */
#define REST_SCENE_BYTES 0x15180
#define REST_SCENE_PITCH 0x168

/* What is presented and where: 312 by 192 taken from scene byte 0x21d8, which
   is scene pixel (24,24), and landing at screen byte 0x504, which is screen
   pixel (4,4) of the mode 13h page -- the ADD EAX,0x21d8 at 000121b4 and the
   four immediates pushed at 00012198 through 000121a7.  VGA_SCREEN_BASE above
   stays a literal for the reason given there: it is where the adapter
   answers, not the address of anything the linker places. */
#define REST_SCENE_WINDOW_AT 0x21d8
#define REST_SCREEN_WINDOW_AT 0x504
#define REST_VIEW_WIDTH 0x138
#define REST_VIEW_ROWS 0xc0

/* VGA input status register 1.  Bit 3 is set while the vertical retrace is in
   progress, which is what the single present straddles. */
#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* fdps_blit_unit_sprite's kernel selector and its mode operand.  Mode 3 is the
   recolour kernel, which reads the operand as tint offset in the low byte,
   colour base in byte 1 and band mask in byte 2; 0xff00 is therefore offset 0,
   base 0xff and mask 0, and a zero mask collapses every pixel of the sprite to
   that one palette index (rlecolor.h).  The unit shows as a flat white
   silhouette. */
#define REST_FLASH_BLIT_MODE 3
#define REST_FLASH_RECOLOR 0xff00

/* The cue the rest plays, MOV EAX,0x61568 / CALL fdps_play_sfx at 000121c2.
   It has to be a plain writable literal: the lookup inside fdps_play_sfx
   upper-cases the caller's own storage in place (vfs.h,
   rebuild_info/pitfalls.md), and the original's copy at 0x61568 is already in
   that case. */
#define REST_SOUND "REST.WAV"

/* PUSH 0x28 / CALL delay at 000121d0: how long the presented frame is held
   before the page is given back. */
#define REST_HOLD_MS 0x28

/* MOV EBX,0x5 / IDIV EBX at 000121e6: the rest gives back a fifth of the
   maximum.  The game's own instructions quote the same 20 percent. */
#define REST_FRACTION_DIVISOR 5

/* 000120d0.  The frame is the plain -4s one: PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP, SUB ESP,0x18, the one argument read from [EBP+0x14], RET with no
   immediate, and every one of the fourteen call sites -- thirteen in
   fdps_map_actor_behavior_step and one at 000160a5 in
   fdps_battle_action_menu -- pushing one dword and doing ADD ESP,0x4
   afterwards.

   THE GUARD IS THREE TESTS AND THE FIRST IS AN EQUALITY.  CMP EAX,[EBP-0xc] /
   JZ at 00012109, on the two HP figures, and then CMP byte ptr [EAX+0x25],0x0
   and CMP byte ptr [EAX+0x26],0x0 on the two status timers.  The HP test is
   `==` and not `>=`: a unit somehow carrying more HP than its maximum is not
   refused here, it rests and is clamped back down at the end.

   BOTH HP FIGURES ARE READ SIGNED AND COMPARED SIGNED.  MOVSX at 000120f5 and
   000120ff widens the two words, and the ceiling test at 000121ff is JLE, the
   signed branch; the fifth is an IDIV with a CDQ-equivalent SAR EDX,0x1f in
   front of it, so it truncates toward zero rather than down
   (rebuild_info/pitfalls.md, contract C).

   NOTHING REPAINTS THE VIEW AFTER THE FLASH.  The sequence ends at free() with
   the white silhouette still standing on screen, and it is the caller that
   clears it; the delay is what the frame is held for, not what the flash lasts.
   fdps_battle_advance_turn runs the same flash and does call
   fdps_render_view_frame afterwards, so copying that shape here, or adding the
   repaint that looks missing, cuts the flash short (unitatk.h).

   THE INDEX IS NOT CHECKED AND NEITHER IS THE ALLOCATION.  unit_index goes
   straight to fdps_get_unit_record, fdps_map_cursor_move_to_unit and
   fdps_blit_unit_sprite, none of which bounds it, and malloc's answer is
   composed into without being tested.

   TWO CALLS' ANSWERS ARE READ.  fdps_get_unit_record's pointer at 000120e7 is
   the record every field comes off and is the record the healed HP is written
   back into, and inp's byte at 0001217b and 0001218c is the retrace bit each
   spin tests.  malloc's block at 00012149 is the scene page.  The remaining
   calls -- fdps_map_cursor_move_to_unit, fdps_draw_scene_layers,
   fdps_blit_unit_sprite, fdps_blit_rect, fdps_play_sfx, delay and free --
   return nothing the original reads. */
int fdps_unit_rest(int unit_index)
{
    /* The record the whole function works on, resolved once and held to the
       end -- nothing in the body moves the unit array. */
    struct fdps_unit_record *unit;
    /* The 360 by 240 page the flash frame is composed on, allocated and freed
       inside the call. */
    unsigned char *scene;
    /* The unit's HP as the call found it, and after the heal the figure that
       is written back into the record. */
    int current_hp;
    /* Its maximum: the figure the fifth is taken of, and the ceiling the heal
       is clamped to. */
    int hp_max;
    /* Whether the unit was in a state to rest, which is what the function
       returns.  Neither caller reads it. */
    int rested;
    /* fdps_blit_unit_sprite's mode operand.  The original parks it in a stack
       slot on entry -- MOV dword ptr [EBP-0x8],0xff00 at 000120dc, above the
       guard and so on the refused path too -- rather than pushing the literal
       at the call site. */
    unsigned int flash_recolor = REST_FLASH_RECOLOR;

    unit = fdps_get_unit_record(unit_index);
    current_hp = (int) unit->hp_current;
    hp_max = (int) unit->hp_max;

    if (current_hp == hp_max
        || unit->status_timers[POISON_TIMER_SLOT] != 0
        || unit->status_timers[PARALYSIS_TIMER_SLOT] != 0) {
        rested = 0;
    } else {
        data_fdps_map_cursor_draw_mode = MAP_CURSOR_HIDDEN;
        fdps_map_cursor_move_to_unit(unit_index);

        scene = (unsigned char *) malloc((size_t) REST_SCENE_BYTES);
        fdps_draw_scene_layers(scene);
        fdps_blit_unit_sprite(scene, unit_index, flash_recolor,
                              REST_FLASH_BLIT_MODE);

        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* Spin until the retrace begins, so the frame that has just been
               composed is the one the monitor shows whole. */
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
            /* And until it ends, so the present starts clear of it. */
        }
        fdps_blit_rect((unsigned int) (scene + REST_SCENE_WINDOW_AT),
                       REST_SCENE_PITCH,
                       (void *) (VGA_SCREEN_BASE + REST_SCREEN_WINDOW_AT),
                       VGA_SCREEN_PITCH, REST_VIEW_WIDTH, REST_VIEW_ROWS);

        fdps_play_sfx(REST_SOUND);
        delay(REST_HOLD_MS);
        free(scene);

        current_hp += hp_max / REST_FRACTION_DIVISOR;
        if (current_hp > hp_max) {
            current_hp = hp_max;
        }
        unit->hp_current = (short) current_hp;

        data_fdps_map_cursor_draw_mode = MAP_CURSOR_PLAIN;
        rested = 1;
    }

    return rested;
}
