/* combat.c -- the full-screen animated attack exchange: the cut-scene the
 * game brings up when two units trade blows, the slide-in of the combatants
 * and the arithmetic that decides what each blow did.
 *
 * See combat.h for what a caller has to know.  This file owns no state of its
 * own: everything it touches is either one of the two unit records or a global
 * src/gamedata.h declares.
 *
 * rand comes from <stdlib.h>.  It is a real call in the original -- CALL
 * 00042cf8 -- and not an inline expansion.  memset comes from <string.h> and
 * is a call as well, CALL 00042cd0, and inp comes from <conio.h> and is the
 * library routine at 0003d4e4 rather than the IN instruction an intrinsic
 * would have produced.
 */
#include <conio.h>
#include <stdlib.h>
#include <string.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "unit.h"
#include "unititem.h"
#include "unitstat.h"
#include "table.h"
#include "maptile.h"
#include "blit.h"
#include "saf.h"
#include "sprite.h"
#include "combat.h"

/* The six ints of the caller's outcome block.  All six are written on entry;
   the middle three are written and never read anywhere in the image, and are
   kept because the stores are three of this function's observable effects. */
#define OUTCOME_MISSED 0
#define OUTCOME_CRITICAL 1
#define OUTCOME_UNUSED_2 2
#define OUTCOME_UNUSED_3 3
#define OUTCOME_UNUSED_4 4
#define OUTCOME_DAMAGE 5

/* The weapon hit effects, read out of item record byte +9 and answered by the
   CMP chain at 0001a161, 0001a172 and 0001a1c0.  Effect 2, the double strike,
   has no arm: the caller reads the same byte for itself and plays a second
   blow, so a double-strike weapon resolves here as an ordinary one. */
#define WEAPON_EFFECT_PARALYSIS 1
#define WEAPON_EFFECT_CRITICAL 3
#define WEAPON_EFFECT_POISON 4

/* The two status timers this function writes, as indices into struct
   fdps_unit_record's status_timers[]: [3] is record offset 0x25 and [4] is
   0x26.  Each is ASSIGNED, so a fresh roll replaces whatever the defender had
   left rather than extending it. */
#define POISON_TIMER_SLOT 3
#define PARALYSIS_TIMER_SLOT 4

/* rand() % 4 + 2 and rand() % 2 + 2: poison runs two to five turns and
   paralysis two or three. */
#define POISON_TURNS_SPREAD 4
#define POISON_TURNS_BASE 2
#define PARALYSIS_TURNS_SPREAD 2
#define PARALYSIS_TURNS_BASE 2

/* Every roll here is a percentage: rand() % 100 against a rate. */
#define PERCENT 100

/* Damage is nine tenths of the stat gap, and the random bonus on top is
   rand() % (damage / 9) -- so the bonus is at most another ninth, and is
   skipped entirely while the damage is below nine. */
#define DAMAGE_NUMERATOR 9
#define DAMAGE_DENOMINATOR 10
#define DAMAGE_SPREAD_DIVISOR 9

/* The class table is indexed by class code PLUS ONE throughout the game; the
   critical rate is byte +8 of that row. */
#define CLASS_RECORD_ROW_BIAS 1

/* Experience is paid on one pairing of sides only: an attacker on side 2
   striking a defender on side 0.  Both halves are equality tests on the side
   byte, so a side-1 attacker pays nothing and a side-2 defender earns nothing
   for being hit. */
#define PLAYER_SIDE 2
#define ENEMY_SIDE 0

/* ENEMYDAT.DAT is indexed by the defender's portrait id less this base.  The
   guard above is on the SIDE byte and not on the portrait id, so the
   subtraction is unchecked: a side-0 defender whose portrait id is below the
   base indexes the table backwards. */
#define FIRST_ENEMY_PORTRAIT_ID 0x3c

/* The divisor is the attacker's own level, raised by 30 whenever its portrait
   id is above 10.  The test is on the portrait id itself and not on whether
   the class has been promoted: as written it catches 蘭斯洛特 (id 0x0b) as
   well as every promoted form (0x0f and up), so writing it as "is a promoted
   class" would hand him more experience than the original does.  What the
   boundary at 10 means beyond that is not established here -- assets/
   characters.md has twelve player characters at ids 00..0b, so it is not the
   line between the roster and anything else. */
#define XP_PENALTY_PORTRAIT_ID_THRESHOLD 10
#define XP_ATTACKER_LEVEL_PENALTY 0x1e

/* 00019f80.  The frame is the plain -4s one: PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP, SUB ESP,0x50, the three arguments read from [EBP+0x14], [EBP+0x18]
   and [EBP+0x1c], and the only caller doing ADD ESP,0xc after the CALL at
   0001987c.  Nothing is returned: EAX holds the damage at the RET only because
   that is the register the last store passed through, and the caller overwrites
   it at 00019884 without looking.

   THE SIX STAT WORDS ARE READ UNSIGNED, and that is the one place this
   function differs from its map-side twin.  Every one of them is loaded XOR
   EAX,EAX / MOV AX,word ptr [...] at 00019fec..0001a057 -- a zero extension --
   where fdps_unit_resolve_attack_hit loads the same six fields of the same
   records with MOVSX at 0001c55f..0001c598.  The fields are signed shorts in
   the settled layout, and fdps_unit_recompute_combat_stats can drive AP, DP,
   HIT and EV below zero because an item's four modifiers are signed shorts
   summed into the total; so the two functions genuinely disagree about what a
   negative stat means, and the casts below are what reproduce this one.  A
   defender whose DP is -1 defends here with 65535 and takes nothing at all,
   and one whose EV is -1 is never hit.  The locals themselves are signed ints
   -- IDIV after SAR EDX,0x1f everywhere -- because the terrain tables hold
   negative percentages and unsigned arithmetic would inflate a stat instead of
   trimming it.

   The order of the rolls is the shape of the function and is not the order a
   reader would guess.  The weapon's status effect is rolled and applied ABOVE
   the accuracy roll -- 0001a161 through 0001a209 all sit above the CALL to
   rand at 0001a20c -- so a poison or paralysis weapon lands its ailment on a
   blow that then misses.  Folding the two status arms into the hit branch,
   which is where they read as though they belong, changes the game
   (rebuild_info/pitfalls.md).

   Terrain comes before all of that, and each combatant is scaled only if
   fdps_unit_is_flying says it is on the ground, so a flying unit neither gains
   nor loses from the tile it is drawn over.  The tile lookup publishes into a
   shared scratch block, so the defender's call overwrites the attacker's and
   each side's table index has to be read while its own call is still the last
   one made.

   Every division is the signed IDIV with the dividend sign extended by SAR
   EDX,0x1f, so all of them truncate toward zero.  That is load-bearing at the
   damage: a gap of -15 gives -13 and not -14 before the clamp turns it into 0,
   and the clamp is a separate CMP/JGE and not a max().

   Three divides have no guard and the original has none either -- the
   experience divides by the attacker's level byte and then by the defender's
   maximum HP.  Only the random damage bonus is guarded, by the != 0 test at
   0001a2a4. */
void fdps_combat_compute_hit_outcome(int attacker_unit_index,
                                     int defender_unit_index,
                                     int *outcome)
{
    struct fdps_unit_record *attacker;
    struct fdps_unit_record *defender;
    struct fdps_class_record *attacker_class;
    struct fdps_item_effect *weapon;
    struct fdps_enemy_data *enemy;
    int attacker_class_code;
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
    unsigned char defender_level;

    damage = 0;

    /* All six written before anything can go wrong, so a caller that never
       reaches the hit branch still reads a fully cleared block. */
    outcome[OUTCOME_MISSED] = 1;
    outcome[OUTCOME_CRITICAL] = 0;
    outcome[OUTCOME_UNUSED_2] = 0;
    outcome[OUTCOME_UNUSED_3] = 0;
    outcome[OUTCOME_UNUSED_4] = 0;
    outcome[OUTCOME_DAMAGE] = 0;

    attacker = fdps_get_unit_record(attacker_unit_index);
    defender = fdps_get_unit_record(defender_unit_index);

    /* The class code is kept as the row it names, and the +1 goes on at the
       call: MOV EAX,[EBP-0x20] / INC EAX / PUSH EAX at 00019ff7. */
    attacker_class_code = attacker->clazz;
    attacker_class = fdps_get_class_record(attacker_class_code +
                                           CLASS_RECORD_ROW_BIAS);
    critical_rate = attacker_class->critical;

    /* The six zero-extended word loads.  The attacker contributes attack and
       accuracy, the defender defense, evasion and both HP figures. */
    attack_power = (int) (unsigned short) attacker->ap;
    defense_power = (int) (unsigned short) defender->dp;
    current_hp = (int) (unsigned short) defender->hp_current;
    max_hp = (int) (unsigned short) defender->hp_max;
    accuracy = (int) (unsigned short) attacker->hit;
    evasion = (int) (unsigned short) defender->ev;

    /* Both levels are copied out as BYTES and stay bytes, so the level penalty
       below is a byte addition that wraps at 256 rather than widening. */
    attacker_level = attacker->level;
    defender_level = defender->level;

    /* The equipped weapon, through the same three calls the caller makes.
       Nothing checks the slot: an attacker with nothing equipped gets -1 back
       and fdps_unit_get_item_id then reads the byte in front of the inventory,
       record offset 0x09, and asks the item table for whatever id that holds. */
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

    /* Terrain, defender side.  Same shape, the other table and the other stat,
       and the tile is the defender's own. */
    if (fdps_unit_is_flying(defender_unit_index) == 0) {
        fdps_map_load_tile_info(defender->pos_x, defender->pos_y);
        defense_power +=
            data_fdps_battle_tile_attr_def_modifier_table
                [data_fdps_map_tile_terrain_type] * defense_power / PERCENT;
    }

    if (hit_effect == WEAPON_EFFECT_CRITICAL) {
        /* A critical weapon does not roll here; its percentage is added to the
           class rate and the single roll below decides. */
        critical_rate += hit_effect_rate;
    } else if (hit_effect == WEAPON_EFFECT_POISON) {
        if (rand() % PERCENT < hit_effect_rate &&
            fdps_unit_is_ailment_immune(defender_unit_index) == 0) {
            defender->status_timers[POISON_TIMER_SLOT] =
                (unsigned char) (rand() % POISON_TURNS_SPREAD +
                                 POISON_TURNS_BASE);
        }
    } else if (hit_effect == WEAPON_EFFECT_PARALYSIS) {
        if (rand() % PERCENT < hit_effect_rate &&
            fdps_unit_is_ailment_immune(defender_unit_index) == 0) {
            defender->status_timers[PARALYSIS_TIMER_SLOT] =
                (unsigned char) (rand() % PARALYSIS_TURNS_SPREAD +
                                 PARALYSIS_TURNS_BASE);
        }
    }

    /* The accuracy roll.  accuracy - evasion is formed as a signed difference
       first and the remainder compared against it with JGE, so a difference of
       100 or more always lands and one of 0 or less never does. */
    if (rand() % PERCENT < accuracy - evasion) {
        outcome[OUTCOME_MISSED] = 0;

        if (rand() % PERCENT < critical_rate) {
            /* SAR-based halving of the terrain-modified defense, so it
               truncates toward zero.  A critical is worth exactly this and
               does not touch the attack side. */
            defense_power = defense_power / 2;
            outcome[OUTCOME_CRITICAL] = 1;
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

        /* The defender's HP is worked out into this local and NOT written
           back; the animation drains the bar from the damage below.  The
           local's only reader is the experience test further down. */
        current_hp -= damage;
        if (current_hp < 0) {
            current_hp = 0;
        }
    }

    if (attacker->side == PLAYER_SIDE && defender->side == ENEMY_SIDE) {
        enemy = fdps_get_enemy_record(defender->portrait_id -
                                      FIRST_ENEMY_PORTRAIT_ID);

        if (attacker->portrait_id > XP_PENALTY_PORTRAIT_ID_THRESHOLD) {
            attacker_level = (unsigned char)
                             (attacker_level + XP_ATTACKER_LEVEL_PENALTY);
        }

        /* Both levels and the reward reach the multiply and the divide as
           zero-extended bytes, so the arithmetic is on small positive integers
           however large the fields look. */
        data_fdps_battle_pending_xp_credit =
            defender_level * enemy->exp_reward / attacker_level;

        /* A survivor pays only the fraction of itself the blow took off, so a
           kill is the only blow that pays the whole award.  The test is on the
           HP the blow would have left, not on whether it landed: a miss leaves
           that non-zero and scales the award by a damage of 0. */
        if (current_hp != 0) {
            data_fdps_battle_pending_xp_credit =
                data_fdps_battle_pending_xp_credit * damage / max_hp;
        }
    }

    outcome[OUTCOME_DAMAGE] = damage;
}

/* The offscreen page the whole combat animation is composed on: 368 bytes to
   the row, 248 rows, and a 24-pixel border on every side so that a sprite
   whose edge runs past the visible window has somewhere to land.  The byte
   count is PUSH 0x16480 at 0001a3a0 and is written as a whole-page constant
   there, not as pitch times rows.  The visible 320x200 window starts at the
   border's corner, 24 rows down and 24 columns in, which is the 0x2298 added
   to the page base at 0001a48e. */
#define COMBAT_SURFACE_PITCH 0x170
#define COMBAT_SURFACE_BYTES 0x16480
#define COMBAT_SURFACE_MARGIN 0x18
#define COMBAT_VISIBLE_ORIGIN_OFFSET 0x2298

/* Eight steps of 40 pixels carry a backdrop exactly one 320-pixel screen, so
   the outgoing picture is one whole screen away and the incoming one exactly
   home when the last step is drawn: PUSH/CMP 0x8 at 0001a38d, IMUL 0x28 at
   0001a3be and 0001a3e9, SUB 0x140 at 0001a3ed. */
#define SLIDE_STEP_COUNT 8
#define SLIDE_PIXELS_PER_STEP 0x28

/* The mode 13h screen: 320 by 200 at the VGA graphics aperture.  The aperture
   is hard-coded in the original -- PUSH 0xa0000 at 0001a47f -- and stays a
   literal here, because it is where the display adapter answers and not the
   address of anything the linker places. */
#define SCREEN_WIDTH 0x140
#define SCREEN_HEIGHT 0xc8
#define VGA_SCREEN_BASE 0x000a0000

/* VGA input status register 1.  Bit 3 is set while the vertical retrace is in
   progress and is the only bit looked at: TEST AL,0x8 at 0001a45b and
   0001a46c. */
#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* 0001a370.  The frame is the plain -4s one: PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP, SUB ESP,0x8, the five arguments read from [EBP+0x14] through
   [EBP+0x24], and both call sites doing ADD ESP,0x14 after the CALL --
   00019a07 and 00019dbb.  Nothing is returned: EAX at the RET is only
   whatever the last load through it left behind, and neither caller looks.

   THE REQUEST IS THE CALLER'S AND ONLY FOUR OF ITS FIELDS ARE WRITTEN.  The
   page pointer, the two extents and the two blend slots are read and never
   stored to, so the caller's page, its 0x170 pitch and its 0xf8 height are
   what the three draws and the present run on.  x, y, the image and the item
   index are rewritten here, and the block comes back holding the last of the
   three draws -- the incoming combatant's frame at the incoming backdrop's
   resting x.  Clearing or restoring them on the way out would be a fifth and
   sixth store this function does not make.

   THE ANIMATION FRAME IS DRAWN BEFORE IT IS ADVANCED.  The third draw takes
   anim_cursor[0] as it stands and fdps_saf_advance_tick moves it afterwards,
   so the eight steps show frames 0 through 7 and leave the cursor on 8.
   Advancing first would drop frame 0 and show frame 8.

   THE THIRD DRAW INHERITS THE INCOMING BACKDROP'S x.  Nothing writes x
   between the second and the third fdps_draw_composite_sprite, which is what
   makes the combatant ride in on its own backdrop rather than stand still
   while it arrives.  Giving the sprite an x of its own -- the reading the
   three near-identical draws invite -- detaches the two.

   THE FRAME-PACING LOCAL IS READ BEFORE IT IS WRITTEN.  MOV EAX,[EBP-0x4] at
   0001a49c is the first reference to that slot in the function, so the first
   of the eight steps compares stack garbage against the tick counter and
   normally falls straight through.  Latching the counter before the loop --
   the obvious way to write this -- adds a tick of delay to the first step
   (rebuild_info/pitfalls.md).  data_fdps_timer_tick_counter is volatile at
   its declaration in gamedata.h because of loops like this one: nothing here
   writes it, so a build allowed to hoist the load would spin forever.

   The two retrace waits straddle the present the way every other presenter in
   the game does: wait for the retrace to begin, and then for it to end, so
   that the 64000-byte transfer starts with the beam off the picture. */
void fdps_combat_slide_backdrops(void *outgoing_backdrop,
                                 void *incoming_backdrop, int *anim_cursor,
                                 int *req, int direction)
{
    /* Which of the eight steps is being drawn, 1 through 8 and never 0: the
       first step already has the outgoing backdrop 40 pixels off centre. */
    int step;
    /* The tick counter's value at the end of the previous step.  Deliberately
       left uninitialised -- see the note above. */
    unsigned int last_tick;

    /* Written once, outside the loop: every draw of every step sits on the
       page's top border. */
    req[DRAW_REQUEST_Y] = COMBAT_SURFACE_MARGIN;

    for (step = 1; step <= SLIDE_STEP_COUNT; step++) {
        memset((void *) req[DRAW_REQUEST_DEST_BASE], 0,
               (size_t) COMBAT_SURFACE_BYTES);

        /* The outgoing backdrop, walking off in the direction given: entry 0
           at 40 pixels further from home on every step. */
        req[DRAW_REQUEST_ITEM_INDEX] = 0;
        req[DRAW_REQUEST_X] = direction * (step * SLIDE_PIXELS_PER_STEP)
                              + COMBAT_SURFACE_MARGIN;
        req[DRAW_REQUEST_IMAGE] = (int) outgoing_backdrop;
        fdps_draw_composite_sprite(req, 0);

        /* The incoming one, one whole screen behind it, so that step 8 puts
           it exactly on the border corner. */
        req[DRAW_REQUEST_X] = direction * (step * SLIDE_PIXELS_PER_STEP
                                           - SCREEN_WIDTH)
                              + COMBAT_SURFACE_MARGIN;
        req[DRAW_REQUEST_IMAGE] = (int) incoming_backdrop;
        fdps_draw_composite_sprite(req, 0);

        /* And the arriving combatant, at whatever x its backdrop just took. */
        req[DRAW_REQUEST_IMAGE] = anim_cursor[SAF_CURSOR_IMAGE];
        req[DRAW_REQUEST_ITEM_INDEX] = anim_cursor[SAF_CURSOR_FRAME_INDEX];
        fdps_draw_composite_sprite(req, 0);

        fdps_saf_advance_tick(anim_cursor, 0);

        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
        }

        fdps_blit_rect((unsigned int) (req[DRAW_REQUEST_DEST_BASE]
                                       + COMBAT_VISIBLE_ORIGIN_OFFSET),
                       COMBAT_SURFACE_PITCH, (void *) VGA_SCREEN_BASE,
                       SCREEN_WIDTH, SCREEN_WIDTH, SCREEN_HEIGHT);

        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
    }
}
