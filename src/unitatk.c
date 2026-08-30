/* unitatk.c -- the on-map physical attack: one blow of a weapon swing
 * resolved from the two units' records, the ground they stand on and the
 * weapon's hit effect.
 *
 * See unitatk.h for what a caller has to know.  The only state this file owns
 * is data_fdps_battle_last_hit_or_miss_flag; everything else it touches is
 * either one of the two unit records or a global gamedata.h declares.
 *
 * rand comes from <stdlib.h> and delay from <i86.h>, which is where Watcom
 * 10.0a declares it; both are real calls in the original -- CALL 00042cf8 and
 * CALL 0003d370 -- and not inline expansions.
 */
#include <stdlib.h>
#include <i86.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "unit.h"
#include "unititem.h"
#include "table.h"
#include "maptile.h"
#include "palette.h"
#include "unitatk.h"

/* The weapon hit effects, read out of item record byte +9 and compared here
   against three of the four values the table uses.  Effect 2, the double
   strike, is deliberately absent: fdps_unit_attack_target reads the same byte
   for itself and answers it by calling this function twice, so the chain below
   passes over it and the blow resolves as an ordinary one. */
#define WEAPON_EFFECT_PARALYSIS 1
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
