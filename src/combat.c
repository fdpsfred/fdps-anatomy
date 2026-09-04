/* combat.c -- the full-screen animated attack exchange: the cut-scene the
 * game brings up when two units trade blows, the slide-in of the combatants
 * and the arithmetic that decides what each blow did.
 *
 * See combat.h for what a caller has to know.  This file owns no state of its
 * own: everything it touches is either one of the two unit records or a global
 * src/gamedata.h declares.
 *
 * rand, malloc and free come from <stdlib.h>.  rand is a real call in the
 * original -- CALL 00042cf8 -- and not an inline expansion, and so are the
 * other two, CALL 0003d375 and CALL 0003d478.  memset comes from <string.h>
 * and is a call as well, CALL 00042cd0, and inp comes from <conio.h> and is
 * the library routine at 0003d4e4 rather than the IN instruction an intrinsic
 * would have produced.  fopen, fread, fclose and sprintf come from <stdio.h>
 * and are the calls at 0004265e, 0004270d, 000428be and 00042d41.
 */
#include <conio.h>
#include <stdio.h>
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
#include "gauge.h"
#include "saf.h"
#include "sprite.h"
#include "vfs.h"
#include "palette.h"
#include "audio.h"
#include "aitarget.h"
#include "cmbblow.h"
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

/* The page's row count, MOV dword ptr [EBP-0x2c],0xf8 at 00019503.  It is the
   same 368 by 248 page with the same 24-pixel apron the slide above composes
   on, so COMBAT_SURFACE_PITCH, COMBAT_SURFACE_BYTES, COMBAT_SURFACE_MARGIN and
   COMBAT_VISIBLE_ORIGIN_OFFSET are that routine's and mean the same here; the
   row count is only spelled out because the slide never writes it. */
#define COMBAT_SURFACE_ROWS 0xf8

/* Nine frames, counted DOWN: MOV dword ptr [EBP-0x10],0x8 at 0001954e and
   CMP dword ptr [EBP-0x10],0x0 / JGE at 00019555, so the counter runs 8 to 0
   inclusive and the last pass is the one that has everything at rest.  The
   counter is how far each element still has to travel, which is why every
   position below is a multiple of it. */
#define ENTRANCE_FIRST_FRAME 8

/* How far the backdrop climbs per frame, IMUL EAX,dword ptr [EBP-0x10],0x14 at
   0001958f: nine frames of 20 rows, so it starts 160 rows below its resting
   place and arrives exactly as the counter reaches 0. */
#define ENTRANCE_BACKDROP_RISE 0x14

/* And how far the attacker moves per frame, MOV dword ptr [EBP-0xc],0xf at
   0001953e against MOV dword ptr [EBP-0xc],0xfffffff1 at 00019547: nine frames
   of 15 columns, so he starts 120 columns out and arrives at the same moment. */
#define ENTRANCE_SLIDE_PER_FRAME 0xf

/* 000194e0.  The frame is the plain -4s one: PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP,
   SUB ESP,0x34, the six arguments read from [EBP+0x14] through [EBP+0x28], and
   both call sites doing ADD ESP,0x18 after the CALL -- 000190ba in
   fdps_combat_play_attack_exchange and 0001a9ac in
   fdps_combat_play_spell_on_targets.  Nothing is returned and neither caller
   looks: both overwrite EAX with a load on the next instruction.

   One branch before the loop, the side test, and one inside it, the
   attacker_only test, so cyclomatic complexity 5 counting the two retrace
   spins and the tick wait.

   THERE IS NO PAGE VARIABLE.  Slot 0 of the draw request IS the page: malloc's
   answer is stored there at 000194f9 and every later use -- the clear at
   0001956f, the two gauge calls at 000195ed and 0001963e, the present's source
   at 00019685 and free's argument at 000196ae -- reads that slot back.  A local
   of its own would be a second slot and a copy into it.

   THE ATTACKER'S y IS WHATEVER THE PREVIOUS DRAW LEFT IN THE REQUEST, and that
   is the whole of the diagonal entrance.  Nothing between the second and the
   third draw writes slot 4: when the defender was drawn it holds the 0x18 that
   draw put there and the attacker slides in level, and when attacker_only is
   set it still holds the backdrop's own climbing y and the attacker rides down
   with it.  Filling in every field of the request before each draw -- the
   obvious way to write three near-identical draws -- pins the attacker at 0x18
   and kills that second mode outright (rebuild_info/pitfalls.md).

   THE CURSOR IS ADVANCED BEFORE THE FRAME IS DRAWN, which is the opposite of
   fdps_combat_slide_backdrops above: fdps_saf_advance_tick is called at
   000195b5 and 00019600 and the two slots are read afterwards at 000195c4 and
   00019608.  So the nine passes show frames 1 through 9 of each clip and frame
   0 is never seen.

   THE ITEM INDEX AND THE y ARE WRITTEN TWICE, once before the loop at 0001950a
   and 0001951f and again inside it at 00019581 and 00019596, and the pre-loop
   pair is dead.  Both stores are in the original and both are kept: the second
   assignment is to a local and no reader can tell either way.

   THE FRAME-PACING LOCAL IS READ BEFORE IT IS WRITTEN.  MOV EAX,dword ptr
   [EBP-0x8] at 00019696 is the first reference to that slot in the function, so
   the first of the nine frames compares stack garbage against the tick counter
   and normally falls straight through.  Latching the counter before the loop
   adds a tick to the first frame (rebuild_info/pitfalls.md).
   data_fdps_timer_tick_counter is volatile at its declaration in gamedata.h
   because of waits like this one: nothing here writes it, so a build allowed to
   hoist the load would spin forever, and the retrace spins read a port and
   cannot be hoisted for the same reason.

   TWO CALLS' ANSWERS ARE READ.  malloc's is the page, and it is read back out
   of slot 0 five times as above; fdps_get_unit_record's is the record whose
   side byte at +6 picks the direction, and it is not kept past that test.
   inp's is tested for bit 3 at both spins.  fdps_saf_advance_tick,
   fdps_draw_composite_sprite, fdps_draw_unit_hp_mp_gauges, fdps_blit_rect,
   memset and free all return nothing the original reads -- the two advances in
   particular throw away the end-of-clip answer, so a clip that runs out inside
   these nine frames wraps and nothing here notices.

   THE PAGE IS NOT CHECKED.  malloc's answer goes straight into the request and
   is cleared through on the first pass, so an exhausted heap faults rather than
   being rejected; adding the null test the obvious reading invites is a branch
   the original does not have.

   The frames are paced by the retrace and by the timer tick, so how many
   instructions stand between them is not observable (contract D). */
void fdps_combat_slide_in_attacker(int attacker_unit_index,
                                   int defender_unit_index, int attacker_only,
                                   int *attacker_saf_cursor,
                                   int *defender_saf_cursor, void *backdrop)
{
    /* The nine-slot block sprite.h describes, and the page with it -- see the
       note above.  Every one of the three draws a frame makes goes through
       this one block, which is what makes the leftovers behaviour. */
    int request[DRAW_REQUEST_DWORDS];
    /* The acting unit's record, read for its side byte and not kept. */
    struct fdps_unit_record *attacker;
    /* How far the attacker still has to move, per frame remaining: negative
       for a side-0 unit, which enters from the left, and positive for anyone
       else, who enters from the right. */
    int slide_step;
    /* How many frames of travel are left, 8 down to 0, so 0 is the frame that
       has everything home. */
    int frames_left;
    /* The tick the previous frame ended on.  Deliberately not initialised --
       see the note above. */
    unsigned int last_tick;

    request[DRAW_REQUEST_DEST_BASE] = (int) malloc((size_t)
                                                   COMBAT_SURFACE_BYTES);
    request[DRAW_REQUEST_DEST_PITCH] = COMBAT_SURFACE_PITCH;
    request[DRAW_REQUEST_DEST_ROWS] = COMBAT_SURFACE_ROWS;
    request[DRAW_REQUEST_ITEM_INDEX] = 0;
    request[DRAW_REQUEST_BLIT_OPERAND] = 0;
    request[DRAW_REQUEST_BLIT_MODE] = 0;
    request[DRAW_REQUEST_Y] = COMBAT_SURFACE_MARGIN;

    attacker = fdps_get_unit_record(attacker_unit_index);
    if (attacker->side == ENEMY_SIDE) {
        slide_step = -ENTRANCE_SLIDE_PER_FRAME;
    } else {
        slide_step = ENTRANCE_SLIDE_PER_FRAME;
    }

    for (frames_left = ENTRANCE_FIRST_FRAME; frames_left >= 0; frames_left--) {
        memset((void *) request[DRAW_REQUEST_DEST_BASE], 0,
               (size_t) COMBAT_SURFACE_BYTES);

        /* The terrain, entry 0, climbing into place under everything else. */
        request[DRAW_REQUEST_IMAGE] = (int) backdrop;
        request[DRAW_REQUEST_ITEM_INDEX] = 0;
        request[DRAW_REQUEST_X] = COMBAT_SURFACE_MARGIN;
        request[DRAW_REQUEST_Y] = frames_left * ENTRANCE_BACKDROP_RISE
                                  + COMBAT_SURFACE_MARGIN;
        fdps_draw_composite_sprite(request, 0);

        /* The unit on the receiving end, already standing where it will stay:
           it is drawn on the border corner every frame and never moves. */
        if (attacker_only == 0) {
            fdps_saf_advance_tick(defender_saf_cursor, 0);
            request[DRAW_REQUEST_Y] = COMBAT_SURFACE_MARGIN;
            request[DRAW_REQUEST_IMAGE] =
                defender_saf_cursor[SAF_CURSOR_IMAGE];
            request[DRAW_REQUEST_ITEM_INDEX] =
                defender_saf_cursor[SAF_CURSOR_FRAME_INDEX];
            fdps_draw_composite_sprite(request, 0);
            fdps_draw_unit_hp_mp_gauges(
                (unsigned char *) request[DRAW_REQUEST_DEST_BASE],
                COMBAT_SURFACE_PITCH, defender_unit_index);
        }

        /* And the attacker, coming in from his own edge -- at whatever y the
           draw before him left behind. */
        fdps_saf_advance_tick(attacker_saf_cursor, 0);
        request[DRAW_REQUEST_IMAGE] = attacker_saf_cursor[SAF_CURSOR_IMAGE];
        request[DRAW_REQUEST_ITEM_INDEX] =
            attacker_saf_cursor[SAF_CURSOR_FRAME_INDEX];
        request[DRAW_REQUEST_X] = frames_left * slide_step
                                  + COMBAT_SURFACE_MARGIN;
        fdps_draw_composite_sprite(request, 0);
        fdps_draw_unit_hp_mp_gauges(
            (unsigned char *) request[DRAW_REQUEST_DEST_BASE],
            COMBAT_SURFACE_PITCH, attacker_unit_index);

        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* Spin until the retrace begins, so the frame just composed is the
               one the monitor shows whole. */
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
            /* And until it ends, so the 64000-byte transfer starts clear. */
        }

        fdps_blit_rect((unsigned int) (request[DRAW_REQUEST_DEST_BASE]
                                       + COMBAT_VISIBLE_ORIGIN_OFFSET),
                       COMBAT_SURFACE_PITCH, (void *) VGA_SCREEN_BASE,
                       SCREEN_WIDTH, SCREEN_WIDTH, SCREEN_HEIGHT);

        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
    }

    free((void *) request[DRAW_REQUEST_DEST_BASE]);
}

/* The two blend tables the whole animation composites through, read straight
   off disk over the globals src/gamedata.h owns: PUSH 0x4800 at 00018d9f for
   the shade ramp and PUSH 0x1000 at 00018ddb for the inverse-palette cube,
   each with an element size of 1.  The fight pair goes in on the way in and
   the map pair comes back on the way out; both names are bare, so they are
   looked for in the working directory. */
#define SHADE_RAMP_BYTES 0x4800
#define PALETTE_CUBE_BYTES 0x1000
#define FIGHT_SHADE_RAMP_FILE "FMer1.tmp"
#define FIGHT_PALETTE_CUBE_FILE "FMer2.tmp"
#define MAP_SHADE_RAMP_FILE "Mer1.tmp"
#define MAP_PALETTE_CUBE_FILE "Mer2.tmp"
#define BLEND_TABLE_MODE "rb"

/* The containers and the members taken out of them.  The three format strings
   are the whole of the naming scheme: a combatant's clips are picked by the
   portrait id at record offset 7 and a terrain by the tile's backdrop id. */
#define MISC_ARCHIVE "MISC.VFS"
#define FIGHT_ARCHIVE "Fight.vfs"
#define FIGACT_ARCHIVE "FigAct.vfs"
#define BACKDROP_ARCHIVE "BackGrnd.vfs"
#define GAUGE_SHEET_MEMBER "FigBar.cel"
#define STAND_CLIP_FORMAT "Stand%03d.saf"
#define ACT_CLIP_FORMAT "Act%03d.saf"
#define BACKDROP_CLIP_FORMAT "Back%02d.saf"

/* The name buffer is sixteen bytes, LEA EAX,[EBP-0x60] against a frame whose
   next slot is at [EBP-0x50].  The longest name any of the three formats can
   produce from a byte is "Stand255.saf", so it fits with room to spare. */
#define CLIP_NAME_BYTES 16

/* The combat gauge fill sheet: PUSH 0x9c4 / CALL malloc at 00018e15 and then
   four passes at 00018e2e-00018e68 decoding FigBar.cel's sprites 4..7 into it
   at a row pitch of 0x7d, x 0 and y = index * 5 (LEA EAX,[EAX+EAX*4]).  The
   four strips share the one buffer, which is why the pitch is the strip's own
   width -- src/gauge.c reads it back the same way. */
#define GAUGE_FILL_SHEET_BYTES 0x9c4
#define GAUGE_FILL_STRIPS 4
#define GAUGE_FIRST_FILL_SPRITE 4
#define GAUGE_FILL_STRIP_PITCH 0x7d
#define GAUGE_FILL_STRIP_HEIGHT 5

/* Both palette uploads cover the whole DAC with no bias: PUSH 0/0/0 then
   PUSH 0xff and PUSH 0 at 00019082 and again at 000191ae. */
#define LAST_DAC_ENTRY 0xff

/* mode 1 of fdps_saf_advance_tick (src/saf.h) is the reset that zeroes a
   cursor's frame and tick counters; the image slot is filled in first. */
#define SAF_CURSOR_RESET 1

/* Byte +4 of a .SAF frame record, the travelling-attack lead-in count
   (resource_info/saf.md).  It is read off frame 0 of the attacker's Act clip
   and nothing here does anything with it but test it against zero and hand it
   on -- fdps_combat_play_blow reads the same byte off the same frame for
   itself at 000198b2. */
#define SAF_FRAME_LEAD_IN_OFFSET 4

/* fdps_check_can_counter_attack (src/aitarget.h) answers 1 or -1 and never 0,
   so both tests here are CMP EAX,0x1 (00018f7b and 000190f5) and neither may
   become a bare predicate. */
#define CAN_COUNTER_ATTACK 1

/* 00018d60.  The frame is the plain -4s one: PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP,
   SUB ESP,0x60, the two arguments read from [EBP+0x14] and [EBP+0x18], and both
   call sites doing ADD ESP,0x8 after the CALL -- 00012fd2 in
   fdps_map_actor_move_and_attack and 00015fa7 in fdps_battle_action_menu.
   Nothing is returned and neither caller looks: both overwrite EAX with a LEA
   on the next instruction.

   FOUR ARGUMENT LISTS ARE NOT WHAT THEY LOOK LIKE, and each of them is a way
   the obvious C differs from the original.

   THE COUNTERBLOW SWAPS EVERYTHING EXCEPT THE BACKDROPS.  The second
   fdps_combat_play_blow at 00019118 gets the two unit indices swapped, the
   defender's Act clip, the two stand cursors swapped -- and [EBP-0x38] pushed
   TWICE, at 000190fc and 00019100, so its from-backdrop and its to-backdrop are
   the same image.  Writing the call as the mirror of the first one, with the
   (defender terrain, attacker terrain) pair the first pass got, makes the view
   slide terrain during the counterblow, which the original never does.

   THE COUNTERATTACK TEST IS ASKED TWICE, at 00018f73 and again at 000190ed, and
   the two answers are not the same question.  It rejects on the defender's
   status_timers[4], and fdps_combat_compute_hit_outcome writes that byte from
   inside the blow when the attacker's weapon lands an ailment.  Hoisting the
   first answer into a local and reusing it -- which is what two identical calls
   invite -- lets a defender this very blow has just paralysed strike back.  The
   write can only turn the answer from yes to no, so the defender's Act clip
   loaded by the first test is always present when the counterblow runs.

   THE LEAD-IN COUNT IS HANDED STRAIGHT TO attacker_only.  PUSH [EBP-0x18] at
   000190a9 is fdps_combat_slide_in_attacker's third argument, so a clip that
   travels brings the attacker on alone: the defender is standing on the terrain
   the view has not reached yet.  It is a frame count used as a flag and not a
   separate decision.

   THE OPENING TERRAIN IS THE ATTACKER'S ONLY WHEN THE CLIP TRAVELS.  The
   defender's tile is resolved first and unconditionally; a non-zero lead-in
   then moves that image into the arrival slot at [EBP-0x30] and resolves the
   attacker's tile over [EBP-0x38], so the pair reaching the blow is (attacker
   terrain, defender terrain) and the animation slides from one to the other.
   With no lead-in the defender's terrain is the only one loaded and the arrival
   slot stays NULL -- which is also what makes the two conditional frees at
   0001915a and 0001916c conditional.

   THE BACKDROP ID IS DECREMENTED IN PLACE, in the global and not in a local:
   CMP byte ptr [0x00069d0b],0x0 / JBE / DEC byte ptr [0x00069d0b] at 00018fdb.
   The compare is unsigned on a byte, so the guard only spares an id of 0; the
   tile carries the id one-based and the file name is zero-based.  The slot is
   shared scratch and does not survive the call: fdps_combat_compute_hit_outcome
   republishes it for both combatants from inside every blow, so what is in it
   on the way out is the blow's last lookup and not this decrement.

   Every byte read out of a record here is zero-extended -- XOR EAX,EAX /
   MOV AL at 00018e88, 00018e93, 00018f60 and 00018fea, AND EAX,0xff at
   00018fc2 and 00018fcd -- so the portrait ids, the tile coordinates and the
   lead-in count are all unsigned (contract C).

   THE CALLS WHOSE ANSWERS ARE READ.  fopen's four are the handle fread and
   fclose are given, and none of the four is tested.  fdps_vfs_load_entry's
   seven are the loaded images, kept to be drawn from and freed, and none is
   tested either -- that function ends the process on a miss.  malloc's is the
   fill sheet, stored into the global and never checked.  fdps_get_unit_record's
   two are the records the ids and tiles come out of.  fdps_saf_get_frame's is
   frame 0 of the Act clip, dereferenced at +4 with no null test.  inp's is
   tested for bit 3 by the one retrace spin.  fdps_check_can_counter_attack's
   two are each compared against 1.  fdps_combat_play_blow's FIRST answer is the
   defender's remaining HP and gates the counterblow; its second is discarded.
   fdps_saf_advance_tick, fdps_cel_blit_sprite, fdps_map_load_tile_info,
   fdps_set_palette_range, fdps_combat_slide_in_attacker,
   fdps_audio_stop_sample, fread, fclose, sprintf, free and memset all return
   nothing the original reads.

   The screen is cleared with memset over the mode 13h aperture rather than
   through a symbol because 0xa0000 is a hardware address and not a relocatable
   object (contract E).  The only loops are the four-pass gauge decode and the
   single retrace spin, and neither paces anything the animation's own timing
   depends on (contract D). */
void fdps_combat_play_attack_exchange(int attacker_unit_index,
                                      int defender_unit_index)
{
    /* The one handle all four blend-table reads pass through, reused. */
    FILE *blend_table_file;
    /* Frame 0 of the attacker's Act clip, read only for its lead-in byte. */
    void *act_first_frame;
    /* The two records, resolved once each and then read for their portrait id
       and their tile coordinates. */
    struct fdps_unit_record *attacker;
    struct fdps_unit_record *defender;
    /* Which of the four gauge fill strips is being decoded, 0 through 3. */
    int strip;
    /* Frame 0's lead-in count: how many frames of the attack clip play before
       the view travels.  Zero means the attack happens where it stands. */
    int lead_in_frames;
    /* The portrait ids the two combatants' clip names are formatted from. */
    int attacker_clip_id;
    int defender_clip_id;
    /* The attacker's attack clip, and the defender's when the counterattack
       test passed on the way in; NULL when it did not. */
    void *attacker_act_clip;
    void *defender_act_clip;
    /* Both combatants' standing clips.  Each is also parked in its cursor's
       image slot, and each is freed through its own local. */
    void *attacker_stand_clip;
    void *defender_stand_clip;
    /* The terrain the animation opens on, and the one it travels to.  With no
       lead-in the first is the defender's and the second stays NULL. */
    void *opening_backdrop;
    void *arrival_backdrop;
    /* The two three-dword playback cursors the presenters advance, one per
       combatant, primed here and owned for the length of the exchange. */
    int attacker_cursor[SAF_CURSOR_DWORDS];
    int defender_cursor[SAF_CURSOR_DWORDS];
    /* Where each member name is formatted before it is looked up. */
    char clip_name[CLIP_NAME_BYTES];

    defender_act_clip = NULL;
    arrival_backdrop = NULL;
    data_fdps_battle_pending_xp_credit = 0;

    blend_table_file = fopen(FIGHT_SHADE_RAMP_FILE, BLEND_TABLE_MODE);
    fread(data_fdps_palette_shade_ramp_table, 1, (size_t) SHADE_RAMP_BYTES,
          blend_table_file);
    fclose(blend_table_file);
    blend_table_file = fopen(FIGHT_PALETTE_CUBE_FILE, BLEND_TABLE_MODE);
    fread(data_fdps_inverse_palette_cube, 1, (size_t) PALETTE_CUBE_BYTES,
          blend_table_file);
    fclose(blend_table_file);

    data_fdps_combat_gauge_sprite_sheet_ptr = (unsigned char *)
        fdps_vfs_load_entry(MISC_ARCHIVE, GAUGE_SHEET_MEMBER);
    data_fdps_gauge_fill_sheet_ptr =
        (unsigned char *) malloc((size_t) GAUGE_FILL_SHEET_BYTES);
    for (strip = 0; strip < GAUGE_FILL_STRIPS; strip++) {
        fdps_cel_blit_sprite(data_fdps_combat_gauge_sprite_sheet_ptr,
                             strip + GAUGE_FIRST_FILL_SPRITE,
                             data_fdps_gauge_fill_sheet_ptr,
                             GAUGE_FILL_STRIP_PITCH, 0,
                             strip * GAUGE_FILL_STRIP_HEIGHT, 0, 0);
    }

    attacker = fdps_get_unit_record(attacker_unit_index);
    defender = fdps_get_unit_record(defender_unit_index);
    attacker_clip_id = (int) attacker->portrait_id;
    defender_clip_id = (int) defender->portrait_id;

    sprintf(clip_name, STAND_CLIP_FORMAT, defender_clip_id);
    defender_stand_clip = fdps_vfs_load_entry(FIGHT_ARCHIVE, clip_name);
    defender_cursor[SAF_CURSOR_IMAGE] = (int) defender_stand_clip;
    fdps_saf_advance_tick(defender_cursor, SAF_CURSOR_RESET);

    sprintf(clip_name, STAND_CLIP_FORMAT, attacker_clip_id);
    attacker_stand_clip = fdps_vfs_load_entry(FIGHT_ARCHIVE, clip_name);
    attacker_cursor[SAF_CURSOR_IMAGE] = (int) attacker_stand_clip;
    fdps_saf_advance_tick(attacker_cursor, SAF_CURSOR_RESET);

    sprintf(clip_name, ACT_CLIP_FORMAT, attacker_clip_id);
    attacker_act_clip = fdps_vfs_load_entry(FIGACT_ARCHIVE, clip_name);
    act_first_frame = fdps_saf_get_frame(attacker_act_clip, 0);
    lead_in_frames = (int) *((unsigned char *) act_first_frame
                             + SAF_FRAME_LEAD_IN_OFFSET);

    if (fdps_check_can_counter_attack(attacker_unit_index, defender_unit_index)
        == CAN_COUNTER_ATTACK) {
        sprintf(clip_name, ACT_CLIP_FORMAT, defender_clip_id);
        defender_act_clip = fdps_vfs_load_entry(FIGACT_ARCHIVE, clip_name);
    }

    while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
        /* One spin only: wait for the retrace to begin, so the load of the
           whole animation starts with the beam off the picture. */
    }

    fdps_map_load_tile_info((int) defender->pos_x, (int) defender->pos_y);
    if (data_fdps_map_tile_combat_backdrop_id > 0) {
        data_fdps_map_tile_combat_backdrop_id--;
    }
    sprintf(clip_name, BACKDROP_CLIP_FORMAT,
            (int) data_fdps_map_tile_combat_backdrop_id);
    opening_backdrop = fdps_vfs_load_entry(BACKDROP_ARCHIVE, clip_name);

    if (lead_in_frames != 0) {
        arrival_backdrop = opening_backdrop;
        fdps_map_load_tile_info((int) attacker->pos_x, (int) attacker->pos_y);
        if (data_fdps_map_tile_combat_backdrop_id > 0) {
            data_fdps_map_tile_combat_backdrop_id--;
        }
        sprintf(clip_name, BACKDROP_CLIP_FORMAT,
                (int) data_fdps_map_tile_combat_backdrop_id);
        opening_backdrop = fdps_vfs_load_entry(BACKDROP_ARCHIVE, clip_name);
    }

    fdps_set_palette_range((struct fdps_palette_entry *)
                           data_fdps_vga_fight_palette_ptr,
                           0, LAST_DAC_ENTRY, 0, 0, 0);
    fdps_combat_slide_in_attacker(attacker_unit_index, defender_unit_index,
                                  lead_in_frames, attacker_cursor,
                                  defender_cursor, opening_backdrop);
    if (fdps_combat_play_blow(attacker_unit_index, defender_unit_index,
                              attacker_act_clip, attacker_cursor,
                              defender_cursor, opening_backdrop,
                              arrival_backdrop) != 0
        && fdps_check_can_counter_attack(attacker_unit_index,
                                         defender_unit_index)
           == CAN_COUNTER_ATTACK) {
        fdps_combat_play_blow(defender_unit_index, attacker_unit_index,
                              defender_act_clip, defender_cursor,
                              attacker_cursor, opening_backdrop,
                              opening_backdrop);
    }

    fdps_audio_stop_sample(SFX_STOP_ALL_SLOTS);
    free(opening_backdrop);
    free(defender_stand_clip);
    free(attacker_stand_clip);
    free(attacker_act_clip);
    if (defender_act_clip != NULL) {
        free(defender_act_clip);
    }
    if (arrival_backdrop != NULL) {
        free(arrival_backdrop);
    }
    free(data_fdps_combat_gauge_sprite_sheet_ptr);
    free(data_fdps_gauge_fill_sheet_ptr);
    memset((void *) VGA_SCREEN_BASE, 0,
           (size_t) (SCREEN_WIDTH * SCREEN_HEIGHT));
    fdps_set_palette_range((struct fdps_palette_entry *)
                           data_fdps_vga_main_palette_ptr,
                           0, LAST_DAC_ENTRY, 0, 0, 0);

    blend_table_file = fopen(MAP_SHADE_RAMP_FILE, BLEND_TABLE_MODE);
    fread(data_fdps_palette_shade_ramp_table, 1, (size_t) SHADE_RAMP_BYTES,
          blend_table_file);
    fclose(blend_table_file);
    blend_table_file = fopen(MAP_PALETTE_CUBE_FILE, BLEND_TABLE_MODE);
    fread(data_fdps_inverse_palette_cube, 1, (size_t) PALETTE_CUBE_BYTES,
          blend_table_file);
    fclose(blend_table_file);
}
