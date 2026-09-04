/* tests/combat.c -- cover for src/combat.c.
 *
 * Every expected value below is read off the assembly of
 * fdps_combat_compute_hit_outcome at 00019f80 -- the six MOV dword ptr
 * [EAX+n],0 stores at 00019f93..00019fce that clear the outcome block, the six
 * XOR EAX,EAX / MOV AX,word ptr loads at 00019fec..0001a057 for which stat
 * comes from which record and how wide each is read, MOV AL,byte ptr [EDX+0x20]
 * / INC EAX for the class row and MOV AL,byte ptr [EDX+0x8] for the critical
 * byte, MOV AL,byte ptr [EDX+0x9] and [EDX+0xa] for the weapon's effect and its
 * rate, the CMP against 0x3, 0x4 and 0x1 that pick the effect, LEA EBX,[EDX+0x2]
 * after IDIV by 4 and by 2 for the two status durations, the CALL to
 * fdps_unit_is_ailment_immune at 0001a194 and 0001a1e2, the SAR pair at
 * 0001a24e that halves the defense, LEA EDX,[EDX+EDX*8] with IDIV 10 for the
 * damage, IDIV 9 for the random bonus, the two JGE clamps at 0001a284 and
 * 0001a2c2, CMP EAX,0x2 on the attacker's side byte and CMP byte ptr
 * [EAX+0x6],0x0 on the defender's, SUB EAX,0x3c on the portrait id, ADD byte
 * ptr [EBP-0x4],0x1e for the level penalty and the two IDIVs that form the
 * experience -- and from the record layouts ticket 17 settled.  None of them is
 * read off the emitted C.
 *
 * THE ONE THING THIS FUNCTION DOES NOT DO is write the defender's HP.  Its
 * map-side twin fdps_unit_resolve_attack_hit ends on MOV word ptr [EDX+0x40],AX;
 * there is no such store anywhere in 00019f80..0001a361, and the HP local is
 * read only by the experience test.  Several cases below assert the record is
 * untouched, because that is what separates the two functions.
 *
 * HOW THE RANDOMNESS IS TAKEN OUT.  Four rolls run through this function and
 * every one is rand() % 100 or rand() % n against a value the fixture chooses,
 * so each is driven to a certain answer rather than mocked:
 *
 *   - the accuracy roll lands on every draw when accuracy - evasion is 100 or
 *     more, and on none when it is 0 or less, because rand() % 100 is 0..99;
 *   - the critical roll is the same, against the critical rate;
 *   - the status roll is the same, against the weapon's percentage;
 *   - the random damage bonus is rand() % (damage / 9), which is skipped
 *     entirely while the damage is below 9 and is identically 0 when that ninth
 *     is exactly 1.  Every damage figure asserted below is one of those cases.
 *
 * The two status durations cannot be pinned to a single value -- they are
 * rand() % 4 + 2 and rand() % 2 + 2 -- so those are asserted as the bounds the
 * assembly gives, over several blows.
 *
 * WHAT IS STAGED.  Every input this function has is a global or a record
 * reached through one: the unit array, the class, item and enemy tables, the
 * four scene layers fdps_map_load_tile_info reads and the two terrain modifier
 * tables.  All of them are pointed at blocks built here.  Nothing below asserts
 * what any of those globals holds on its own -- ticket 23 owns that -- and the
 * two modifier tables are zeroed by the fixture so that a test which does not
 * name a terrain effect cannot inherit one.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "audio.h"
#include "vfs.h"
#include "sprite.h"
#include "combat.h"

/* The strides the four accessors in src/table.c and src/unit.c multiply by. */
#define UNIT_RECORD_STRIDE 0x50
#define ITEM_RECORD_STRIDE 0x17
#define CLASS_RECORD_STRIDE 0x0a
#define ENEMY_RECORD_STRIDE 0x0a

/* The two combatants' places in the staged unit array.  Four records are
   staged so that a walk which strayed into a neighbour would be visible. */
#define STAGE_UNITS 4
#define ATTACKER 0
#define DEFENDER 1

#define STAGE_ITEMS 256
#define STAGE_CLASSES 64
#define STAGE_ENEMIES 16

/* The scene layers, in the shape src/maptile.c reads them: a terrain layer
   whose signed 16-bit tile width is at +7 and whose 16-bit tile ids start at
   +0x0b, an attribute table whose 4-byte rows start at +0x11, a movement grid
   of 2-byte cells after a 4-byte header, and an event layer whose own width is
   at +7 and whose cell bytes start at +0x10. */
#define TERRAIN_CELLS_AT 0x0b
#define ATTR_ROWS_AT 0x11
#define EVENT_CELLS_AT 0x10
#define MAP_WIDTH 4
#define MAP_CELLS 16
#define ATTR_ROWS 16

/* The attribute rows the two combatants' tiles select.  The attacker stands on
   (0, 0), which is cell 0 and so tile id 0, and the defender on (1, 0), which
   is cell 1 and tile id 1 -- two different rows, so each side's terrain class
   is its own and a body that used one tile for both would be visible. */
#define ATTACKER_ATTR_ROW 0
#define DEFENDER_ATTR_ROW 1

/* The weapon hit effect codes, from the CMP chain at 0001a161, 0001a172 and
   0001a1c0.  Effect 2, the double strike, is answered by the caller and has no
   arm here. */
#define EFFECT_NONE 0
#define EFFECT_PARALYSIS 1
#define EFFECT_DOUBLE_STRIKE 2
#define EFFECT_CRITICAL 3
#define EFFECT_POISON 4

/* One of the class codes fdps_unit_is_ailment_immune answers 1 for, and one of
   the portrait ids it answers 1 for.  This function has no immunity test of its
   own: it asks that function, so both routes have to work here. */
#define CLASS_IMMUNE 0x19
#define PORTRAIT_IMMUNE 0x40

/* One of the five class codes fdps_unit_is_flying answers 1 for, used here to
   take a combatant out of the terrain path.  It is in neither immune class
   span, so it changes nothing but the terrain. */
#define CLASS_FLYING 0x1f

/* status_timers[3] at record offset 0x25 and [4] at 0x26. */
#define POISON_SLOT 3
#define PARALYSIS_SLOT 4

/* AND AL,0x40 in fdps_unit_find_equipped_slot: the equipped bit of an
   inventory entry's flag byte.  An entry's second byte is the item id. */
#define ENTRY_EQUIPPED 0x40

/* Item type 1 is inside the weapon span 1..0x15, so an item staged with it is
   accepted by the equipped-weapon search. */
#define WEAPON_ITEM_TYPE 1

/* The item id the attacker carries in the standard fixture. */
#define FIXTURE_WEAPON_ID 1

/* CMP EAX,0x2 on the attacker's side byte, CMP byte ptr [EAX+0x6],0x0 on the
   defender's, and SUB EAX,0x3c on the defender's portrait id. */
#define PLAYER_SIDE 2
#define ENEMY_SIDE 0
#define FIRST_ENEMY_PORTRAIT 0x3c

/* The outcome block the function fills, and a value no field of it could
   legitimately hold, so "not written" is distinguishable from "written 0". */
#define OUTCOME_SLOTS 6
#define OUTCOME_MISSED 0
#define OUTCOME_CRITICAL 1
#define OUTCOME_DAMAGE 5
#define OUTCOME_SENTINEL 0x5a5a

/* Likewise for the experience global, whose every legitimate figure here is
   non-negative. */
#define XP_SENTINEL (-1)

static unsigned char unit_block[STAGE_UNITS * UNIT_RECORD_STRIDE];
static unsigned char item_block[STAGE_ITEMS * ITEM_RECORD_STRIDE];
static unsigned char class_block[STAGE_CLASSES * CLASS_RECORD_STRIDE];
static unsigned char enemy_block[STAGE_ENEMIES * ENEMY_RECORD_STRIDE];

static unsigned char stage_tile_map[TERRAIN_CELLS_AT + MAP_CELLS * 2];
static unsigned char stage_attr[ATTR_ROWS_AT + ATTR_ROWS * 4];
static unsigned char stage_grid[4 + MAP_CELLS * 2];
static unsigned char stage_event[EVENT_CELLS_AT + MAP_CELLS];

/* The caller's block, plus one guard int on each side so that a store past
   either end of the six would be caught. */
static int outcome[OUTCOME_SLOTS + 2];

/* Only ever read for its field offsets and sizes. */
static struct fdps_unit_record layout_probe;

static struct fdps_unit_record *unit(int unit_index)
{
    return (struct fdps_unit_record *)
        (unit_block + unit_index * UNIT_RECORD_STRIDE);
}

static struct fdps_item_effect *item(int item_id)
{
    return (struct fdps_item_effect *)
        (item_block + item_id * ITEM_RECORD_STRIDE);
}

static struct fdps_class_record *class_row(int row_index)
{
    return (struct fdps_class_record *)
        (class_block + row_index * CLASS_RECORD_STRIDE);
}

static struct fdps_enemy_data *enemy_row(int enemy_index)
{
    return (struct fdps_enemy_data *)
        (enemy_block + enemy_index * ENEMY_RECORD_STRIDE);
}

/* The six slots start at index 1 of the guarded array. */
static int *block(void)
{
    return outcome + 1;
}

static int slot(int which)
{
    return outcome[1 + which];
}

/* Builds the standard duel and publishes every global the function reads.
 *
 * The attacker is a side-1 roster unit with a plain equipped weapon; the
 * defender is a level 10 side-0 unit with 20 of 40 HP.  Accuracy 200 against
 * evasion 0 is a difference of 200, so the blow lands on every draw, and AP 100
 * against DP 91 is a gap of 9, which gives a damage of exactly 8 with no random
 * bonus.  The attacker is on side 1 and not 2, so the baseline blow pays no
 * experience and the sentinel survives.
 *
 * Both terrain modifier tables are cleared, so terrain is neutral until a test
 * asks for it; both combatants' class is 0, which is neither one of the five
 * flying codes nor an immune one, so the terrain path does run and the status
 * arms are reachable.
 *
 * The outcome block is filled with the sentinel, so every one of the six stores
 * has to happen for the assertions below to read 0. */
static void stage(void)
{
    int i;

    for (i = 0; i < (int) sizeof(unit_block); i++) {
        unit_block[i] = 0;
    }
    for (i = 0; i < (int) sizeof(item_block); i++) {
        item_block[i] = 0;
    }
    for (i = 0; i < (int) sizeof(class_block); i++) {
        class_block[i] = 0;
    }
    for (i = 0; i < (int) sizeof(enemy_block); i++) {
        enemy_block[i] = 0;
    }

    /* The terrain layer: cell n holds tile id n, so the attribute row a
       combatant selects is the number of the cell it stands on. */
    for (i = 0; i < TERRAIN_CELLS_AT; i++) {
        stage_tile_map[i] = 0xaa;
    }
    *(short *) (stage_tile_map + 7) = (short) MAP_WIDTH;
    for (i = 0; i < MAP_CELLS; i++) {
        *(short *) (stage_tile_map + TERRAIN_CELLS_AT + i * 2) = (short) i;
    }

    for (i = 0; i < (int) sizeof(stage_attr); i++) {
        stage_attr[i] = 0;
    }
    for (i = 0; i < (int) sizeof(stage_grid); i++) {
        stage_grid[i] = 0;
    }
    *(short *) stage_grid = (short) MAP_WIDTH;

    for (i = 0; i < (int) sizeof(stage_event); i++) {
        stage_event[i] = 0;
    }
    *(short *) (stage_event + 7) = (short) MAP_WIDTH;

    data_fdps_map_unit_array_ptr = unit_block;
    data_fdps_item_effect_table_ptr = item_block;
    data_fdps_class_table_ptr = class_block;
    data_fdps_battle_enemy_data_table_ptr = enemy_block;
    data_fdps_scene_layer_tile_map_ptrs[0] = stage_tile_map;
    data_fdps_scene_layer_tile_attr_ptr[0] = stage_attr;
    data_fdps_battle_move_grid_ptr = stage_grid;
    data_fdps_map_cell_event_code_layer_ptr = stage_event;

    for (i = 0; i < 6; i++) {
        data_fdps_battle_tile_attr_ap_modifier_table[i] = 0;
        data_fdps_battle_tile_attr_def_modifier_table[i] = 0;
    }

    unit(ATTACKER)->pos_x = 0;
    unit(ATTACKER)->pos_y = 0;
    unit(ATTACKER)->side = 1;
    unit(ATTACKER)->portrait_id = 5;
    unit(ATTACKER)->clazz = 0;
    unit(ATTACKER)->level = 5;
    unit(ATTACKER)->ap = 100;
    unit(ATTACKER)->hit = 200;
    unit(ATTACKER)->inventory_slots[0] = ENTRY_EQUIPPED;
    unit(ATTACKER)->inventory_slots[1] = FIXTURE_WEAPON_ID;

    unit(DEFENDER)->pos_x = 1;
    unit(DEFENDER)->pos_y = 0;
    unit(DEFENDER)->side = ENEMY_SIDE;
    unit(DEFENDER)->portrait_id = 5;
    unit(DEFENDER)->clazz = 0;
    unit(DEFENDER)->level = 10;
    unit(DEFENDER)->dp = 91;
    unit(DEFENDER)->ev = 0;
    unit(DEFENDER)->hp_current = 20;
    unit(DEFENDER)->hp_max = 40;

    item(FIXTURE_WEAPON_ID)->type = WEAPON_ITEM_TYPE;
    item(FIXTURE_WEAPON_ID)->hit_effect = EFFECT_NONE;
    item(FIXTURE_WEAPON_ID)->hit_effect_rate = 0;

    /* The attacker's class is 0, so the critical rate comes from class row 1. */
    class_row(1)->critical = 0;

    enemy_row(0)->exp_reward = 60;
    enemy_row(1)->exp_reward = 30;

    data_fdps_battle_pending_xp_credit = XP_SENTINEL;

    for (i = 0; i < OUTCOME_SLOTS + 2; i++) {
        outcome[i] = OUTCOME_SENTINEL;
    }
}

static void set_accuracy(int accuracy, int evasion)
{
    unit(ATTACKER)->hit = (short) accuracy;
    unit(DEFENDER)->ev = (short) evasion;
}

static void set_power(int attack, int defense)
{
    unit(ATTACKER)->ap = (short) attack;
    unit(DEFENDER)->dp = (short) defense;
}

static void set_hp(int current, int maximum)
{
    unit(DEFENDER)->hp_current = (short) current;
    unit(DEFENDER)->hp_max = (short) maximum;
}

static void set_weapon(int effect, int rate)
{
    item(FIXTURE_WEAPON_ID)->hit_effect = (unsigned char) effect;
    item(FIXTURE_WEAPON_ID)->hit_effect_rate = (unsigned char) rate;
}

/* The terrain class each combatant's own tile reports, written into byte +2 of
   that tile's attribute row. */
static void set_terrain(int attacker_terrain, int defender_terrain)
{
    stage_attr[ATTR_ROWS_AT + ATTACKER_ATTR_ROW * 4 + 2] =
        (unsigned char) attacker_terrain;
    stage_attr[ATTR_ROWS_AT + DEFENDER_ATTR_ROW * 4 + 2] =
        (unsigned char) defender_terrain;
}

/* Turns the duel into the one pairing of sides that pays experience, and gives
   the defender a portrait id inside ENEMYDAT.DAT's range so that the record
   index the award uses is not negative. */
static void make_it_an_award_setup(void)
{
    unit(ATTACKER)->side = PLAYER_SIDE;
    unit(DEFENDER)->side = ENEMY_SIDE;
    unit(DEFENDER)->portrait_id = FIRST_ENEMY_PORTRAIT;
}

/* The offsets the body reads have to be the settled layouts' own, or the C
   addresses different bytes from the original. */
static void the_record_layouts_match_the_offsets_read(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), UNIT_RECORD_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_x), 0x00);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_y), 0x01);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, side), 0x06);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, portrait_id), 0x07);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, reserved_09), 0x09);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, clazz), 0x20);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, level), 0x21);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, status_timers), 0x22);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, hp_current), 0x40);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, hp_max), 0x42);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, ap), 0x48);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, dp), 0x4a);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, hit), 0x4c);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, ev), 0x4e);
    CHECK_EQ((int) sizeof(layout_probe.hp_current), 2);

    CHECK_EQ((int) sizeof(struct fdps_item_effect), ITEM_RECORD_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_item_effect, hit_effect), 0x09);
    CHECK_EQ((int) offsetof(struct fdps_item_effect, hit_effect_rate), 0x0a);

    CHECK_EQ((int) sizeof(struct fdps_class_record), CLASS_RECORD_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_class_record, critical), 0x08);

    CHECK_EQ((int) sizeof(struct fdps_enemy_data), ENEMY_RECORD_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_enemy_data, exp_reward), 0x09);
}

/* All six dwords are stored before anything else runs, so the three the
   program never reads are still cleared, and nothing outside the six is
   touched.  The sentinel is what makes "written 0" visible. */
static void every_one_of_the_six_slots_is_written(void)
{
    stage();
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(2), 0);
    CHECK_EQ(slot(3), 0);
    CHECK_EQ(slot(4), 0);
    CHECK_EQ(outcome[0], OUTCOME_SENTINEL);
    CHECK_EQ(outcome[OUTCOME_SLOTS + 1], OUTCOME_SENTINEL);

    /* And on the path that returns earliest -- a miss -- as well. */
    stage();
    set_accuracy(0, 0);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(2), 0);
    CHECK_EQ(slot(3), 0);
    CHECK_EQ(slot(4), 0);
    CHECK_EQ(outcome[0], OUTCOME_SENTINEL);
    CHECK_EQ(outcome[OUTCOME_SLOTS + 1], OUTCOME_SENTINEL);
}

/* The baseline blow: it lands, so slot 0 is cleared, and it reports 8. */
static void a_landed_blow_clears_the_miss_flag_and_reports_its_damage(void)
{
    stage();
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_MISSED), 0);
    CHECK_EQ(slot(OUTCOME_CRITICAL), 0);
    CHECK_EQ(slot(OUTCOME_DAMAGE), 8);
}

/* A miss keeps the 1 the entry store put in slot 0 and reports no damage. */
static void a_missed_blow_reports_missed_and_no_damage(void)
{
    stage();
    set_accuracy(0, 0);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_MISSED), 1);
    CHECK_EQ(slot(OUTCOME_DAMAGE), 0);

    stage();
    set_accuracy(50, 50);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_MISSED), 1);

    stage();
    set_accuracy(0, 100);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_MISSED), 1);
}

/* The defender's record is left alone.  There is no store to +0x40 anywhere in
   the function: the HP it works out lives in a local and is read only by the
   experience test.  A blow that would kill outright still leaves the record
   holding what it held. */
static void the_defenders_hp_record_is_never_written(void)
{
    stage();
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(unit(DEFENDER)->hp_current, 20);
    CHECK_EQ(unit(DEFENDER)->hp_max, 40);

    stage();
    set_hp(5, 40);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_DAMAGE), 8);
    CHECK_EQ(unit(DEFENDER)->hp_current, 5);
}

/* The roll is against accuracy MINUS evasion and not against accuracy alone:
   100 against 0 lands on every draw, and the same 100 against 100 lands on
   none. */
static void evasion_is_subtracted_from_the_accuracy(void)
{
    stage();
    set_accuracy(100, 0);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_MISSED), 0);

    stage();
    set_accuracy(100, 100);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_MISSED), 1);

    stage();
    set_accuracy(250, 150);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_MISSED), 0);
}

/* Damage is the stat gap times nine over ten, truncated: a gap of 9 gives 8, a
   gap of 2 gives 1 and a gap of 1 gives nothing at all -- and that last case is
   still a HIT, which is what tells a damage of 0 apart from a miss.  A gap of
   10 gives 9, whose ninth is exactly 1, and rand() % 1 is 0, so the random
   bonus arm is reached and contributes nothing. */
static void damage_is_nine_tenths_of_the_stat_gap(void)
{
    stage();
    set_power(100, 91);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_DAMAGE), 8);

    stage();
    set_power(100, 98);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_DAMAGE), 1);

    stage();
    set_power(100, 99);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_DAMAGE), 0);
    CHECK_EQ(slot(OUTCOME_MISSED), 0);

    stage();
    set_power(100, 90);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_DAMAGE), 9);
}

/* A defender stronger than the attacker deals no negative damage.  The clamp is
   a separate CMP/JGE against 0 and not a max(): without it the reported figure
   would be -13 and the animation would heal the target. */
static void a_negative_damage_is_clamped_to_zero(void)
{
    stage();
    set_power(5, 20);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_DAMAGE), 0);
    CHECK_EQ(slot(OUTCOME_MISSED), 0);

    stage();
    set_power(0, 1000);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_DAMAGE), 0);
}

/* The six stat words are ZERO extended -- XOR EAX,EAX / MOV AX -- where the
   map-side twin sign extends the same six fields with MOVSX.  A defender whose
   DP word is -1 therefore defends with 65535 and takes nothing at all, and one
   whose EV word is -1 evades everything.  Read signed, the same two records
   would give a damage of 90 and a certain hit. */
static void the_stat_words_are_read_unsigned(void)
{
    stage();
    set_power(100, -1);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_MISSED), 0);
    CHECK_EQ(slot(OUTCOME_DAMAGE), 0);

    stage();
    set_accuracy(200, -1);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_MISSED), 1);
}

/* The same zero extension on the HP word, seen through the only reader the HP
   local has.  A current HP of -1 is 65535, so 65535 - 8 is not zero and the
   award is scaled by damage over max HP -- 120 * 8 / 40 -- where a sign
   extended -1 would clamp to 0 and pay the whole 120. */
static void the_hp_word_is_read_unsigned_too(void)
{
    stage();
    make_it_an_award_setup();
    set_hp(-1, 40);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(data_fdps_battle_pending_xp_credit, 24);
}

/* A critical halves the defender's defense, is reported in slot 1 and touches
   nothing else: with AP 14 against DP 10 the gap is 4 and the damage 3, while
   the halved DP of 5 makes the gap 9 and the damage 8.  The rate comes from
   byte +8 of class row clazz + 1. */
static void a_critical_halves_the_defense_and_is_reported(void)
{
    stage();
    set_power(14, 10);
    class_row(1)->critical = 0;
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_CRITICAL), 0);
    CHECK_EQ(slot(OUTCOME_DAMAGE), 3);

    stage();
    set_power(14, 10);
    class_row(1)->critical = 100;
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_CRITICAL), 1);
    CHECK_EQ(slot(OUTCOME_DAMAGE), 8);
}

/* A blow that misses is never critical: the critical roll sits inside the hit
   branch, unlike the status rolls. */
static void a_missed_blow_is_never_critical(void)
{
    stage();
    set_accuracy(0, 0);
    class_row(1)->critical = 100;
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_MISSED), 1);
    CHECK_EQ(slot(OUTCOME_CRITICAL), 0);
}

/* The class row is clazz + 1 and not clazz: an attacker of class 3 takes its
   critical rate from row 4, and row 3 is the neighbour that must not be read. */
static void the_critical_rate_comes_from_the_next_class_row(void)
{
    stage();
    set_power(14, 10);
    unit(ATTACKER)->clazz = 3;
    class_row(3)->critical = 0;
    class_row(4)->critical = 100;
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_CRITICAL), 1);

    stage();
    set_power(14, 10);
    unit(ATTACKER)->clazz = 3;
    class_row(3)->critical = 100;
    class_row(4)->critical = 0;
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_CRITICAL), 0);
}

/* Hit effect 3 does not roll: its percentage is added to the class rate and the
   ordinary critical roll decides. */
static void a_critical_weapon_adds_its_rate_to_the_class_rate(void)
{
    stage();
    set_power(14, 10);
    set_weapon(EFFECT_CRITICAL, 100);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_CRITICAL), 1);
    CHECK_EQ(slot(OUTCOME_DAMAGE), 8);

    stage();
    set_power(14, 10);
    set_weapon(EFFECT_CRITICAL, 0);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_CRITICAL), 0);
}

/* The weapon is the ATTACKER's equipped one.  The same critical weapon in the
   defender's hands changes nothing. */
static void the_weapon_is_the_attackers_own(void)
{
    stage();
    set_power(14, 10);
    item(2)->type = WEAPON_ITEM_TYPE;
    item(2)->hit_effect = EFFECT_CRITICAL;
    item(2)->hit_effect_rate = 100;
    unit(DEFENDER)->inventory_slots[0] = ENTRY_EQUIPPED;
    unit(DEFENDER)->inventory_slots[1] = 2;
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_CRITICAL), 0);

    stage();
    set_power(14, 10);
    item(2)->type = WEAPON_ITEM_TYPE;
    item(2)->hit_effect = EFFECT_CRITICAL;
    item(2)->hit_effect_rate = 100;
    unit(ATTACKER)->inventory_slots[1] = 2;
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_CRITICAL), 1);
}

/* Nothing checks the slot.  An attacker with nothing equipped gets -1 back and
   the id lookup then reads the byte in front of the inventory, record offset
   0x09, and asks the item table for whatever id that holds. */
static void an_unarmed_attacker_reads_the_byte_before_the_inventory(void)
{
    stage();
    set_power(14, 10);
    unit(ATTACKER)->inventory_slots[0] = 0;
    unit(ATTACKER)->reserved_09 = 7;
    item(7)->hit_effect = EFFECT_CRITICAL;
    item(7)->hit_effect_rate = 100;
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_CRITICAL), 1);

    stage();
    set_power(14, 10);
    unit(ATTACKER)->inventory_slots[0] = 0;
    unit(ATTACKER)->reserved_09 = 8;
    item(8)->hit_effect = EFFECT_NONE;
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_CRITICAL), 0);
}

/* Effect 4 writes status_timers[3] and only that one, with a duration of
   rand() % 4 + 2, so it is 2, 3, 4 or 5 and never 0 or 1.  Eight blows are
   struck so that the bound is asserted against several draws. */
static void poison_lasts_two_to_five_turns(void)
{
    int draw;
    int duration;
    int lowest;
    int highest;

    lowest = 99;
    highest = -1;
    for (draw = 0; draw < 8; draw++) {
        stage();
        set_weapon(EFFECT_POISON, 100);
        fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
        duration = (int) unit(DEFENDER)->status_timers[POISON_SLOT];
        if (duration < lowest) {
            lowest = duration;
        }
        if (duration > highest) {
            highest = duration;
        }
        CHECK_EQ(unit(DEFENDER)->status_timers[PARALYSIS_SLOT], 0);
    }
    CHECK_EQ(lowest >= 2, 1);
    CHECK_EQ(highest <= 5, 1);
}

/* Effect 1 writes status_timers[4] with rand() % 2 + 2, so it is 2 or 3. */
static void paralysis_lasts_two_or_three_turns(void)
{
    int draw;
    int duration;
    int lowest;
    int highest;

    lowest = 99;
    highest = -1;
    for (draw = 0; draw < 6; draw++) {
        stage();
        set_weapon(EFFECT_PARALYSIS, 100);
        fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
        duration = (int) unit(DEFENDER)->status_timers[PARALYSIS_SLOT];
        if (duration < lowest) {
            lowest = duration;
        }
        if (duration > highest) {
            highest = duration;
        }
        CHECK_EQ(unit(DEFENDER)->status_timers[POISON_SLOT], 0);
    }
    CHECK_EQ(lowest >= 2, 1);
    CHECK_EQ(highest <= 3, 1);
}

/* A rate of 0 never rolls under itself, so neither timer is written. */
static void a_zero_rate_never_lands_a_status(void)
{
    stage();
    set_weapon(EFFECT_POISON, 0);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(unit(DEFENDER)->status_timers[POISON_SLOT], 0);

    stage();
    set_weapon(EFFECT_PARALYSIS, 0);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(unit(DEFENDER)->status_timers[PARALYSIS_SLOT], 0);
}

/* The contract the whole function turns on: the status roll sits ABOVE the
   accuracy roll and is not inside it, so the ailment lands on a blow that then
   misses.  The reported outcome still says missed with no damage, which is what
   says the blow really did miss. */
static void a_status_lands_even_when_the_blow_misses(void)
{
    stage();
    set_accuracy(0, 0);
    set_weapon(EFFECT_POISON, 100);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_MISSED), 1);
    CHECK_EQ(slot(OUTCOME_DAMAGE), 0);
    CHECK_EQ(unit(DEFENDER)->status_timers[POISON_SLOT] >= 2, 1);

    stage();
    set_accuracy(0, 0);
    set_weapon(EFFECT_PARALYSIS, 100);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_MISSED), 1);
    CHECK_EQ(unit(DEFENDER)->status_timers[PARALYSIS_SLOT] >= 2, 1);
}

/* Immunity is asked of fdps_unit_is_ailment_immune and is not a class test of
   this function's own, so BOTH of that function's routes have to work here: the
   class code 0x19 and the portrait id span 0x3c..0x44.  A defender immune by
   portrait alone, with a perfectly ordinary class, still takes no ailment --
   and takes the damage as usual either way. */
static void an_immune_defender_takes_no_status_by_either_route(void)
{
    stage();
    unit(DEFENDER)->clazz = CLASS_IMMUNE;
    set_weapon(EFFECT_POISON, 100);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(unit(DEFENDER)->status_timers[POISON_SLOT], 0);
    CHECK_EQ(slot(OUTCOME_DAMAGE), 8);

    stage();
    unit(DEFENDER)->clazz = CLASS_IMMUNE;
    set_weapon(EFFECT_PARALYSIS, 100);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(unit(DEFENDER)->status_timers[PARALYSIS_SLOT], 0);

    stage();
    unit(DEFENDER)->portrait_id = PORTRAIT_IMMUNE;
    set_weapon(EFFECT_POISON, 100);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(unit(DEFENDER)->status_timers[POISON_SLOT], 0);
    CHECK_EQ(slot(OUTCOME_DAMAGE), 8);
}

/* Effect 2 has no arm here: it lands no ailment and adds nothing to the
   critical rate, so the blow resolves as an ordinary one. */
static void the_double_strike_effect_is_not_handled_here(void)
{
    stage();
    set_power(14, 10);
    set_weapon(EFFECT_DOUBLE_STRIKE, 100);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_CRITICAL), 0);
    CHECK_EQ(slot(OUTCOME_DAMAGE), 3);
    CHECK_EQ(unit(DEFENDER)->status_timers[POISON_SLOT], 0);
    CHECK_EQ(unit(DEFENDER)->status_timers[PARALYSIS_SLOT], 0);
}

/* The attack is scaled by the ATTACK table at the attacker's own terrain class:
   +50 percent turns an AP of 100 into 150, which against a DP of 141 is a gap
   of 9 and a damage of 8.  Without the scaling the same blow cannot reach the
   defender at all.  The attacker's tile reports class 1 and the defender's
   class 2, so an entry read from the wrong tile's class would be the zero the
   fixture left. */
static void terrain_scales_the_attackers_attack(void)
{
    stage();
    set_terrain(1, 2);
    set_power(100, 141);
    data_fdps_battle_tile_attr_ap_modifier_table[1] = 50;
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_DAMAGE), 8);

    stage();
    set_terrain(1, 2);
    set_power(100, 141);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_DAMAGE), 0);
}

/* The defense is scaled by the DEFENSE table at the defender's own terrain
   class, and the entry's sign is behaviour: +50 percent turns a DP of 10 into
   15, which against an AP of 24 is a gap of 9 and a damage of 8, while the
   unscaled gap of 14 gives 12.  A NEGATIVE entry lowers the defense instead --
   -50 percent turns a DP of 20 into 10, which against an AP of 19 becomes a gap
   of 9 where the unscaled blow could not connect. */
static void terrain_scales_the_defenders_defense(void)
{
    stage();
    set_terrain(1, 2);
    set_power(24, 10);
    data_fdps_battle_tile_attr_def_modifier_table[2] = 50;
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_DAMAGE), 8);

    stage();
    set_terrain(1, 2);
    set_power(24, 10);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_DAMAGE), 12);

    stage();
    set_terrain(1, 2);
    set_power(19, 20);
    data_fdps_battle_tile_attr_def_modifier_table[2] = -50;
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_DAMAGE), 8);

    stage();
    set_terrain(1, 2);
    set_power(19, 20);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_DAMAGE), 0);
}

/* A flying combatant skips its own terrain lookup and keeps its raw stat, while
   the other side's scaling is unaffected. */
static void a_flying_combatant_is_not_scaled_by_terrain(void)
{
    stage();
    set_terrain(1, 2);
    set_power(100, 141);
    data_fdps_battle_tile_attr_ap_modifier_table[1] = 50;
    unit(ATTACKER)->clazz = CLASS_FLYING;
    class_row(CLASS_FLYING + 1)->critical = 0;
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_DAMAGE), 0);

    stage();
    set_terrain(1, 2);
    set_power(24, 10);
    data_fdps_battle_tile_attr_def_modifier_table[2] = 50;
    unit(DEFENDER)->clazz = CLASS_FLYING;
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_DAMAGE), 12);
}

/* The award is defender level times the enemy record's reward over the
   attacker's level: 10 * 60 / 5 is 120, and a blow that leaves no HP pays the
   whole of it. */
static void a_kill_pays_the_whole_award(void)
{
    stage();
    make_it_an_award_setup();
    set_hp(8, 40);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_DAMAGE), 8);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, 120);
}

/* A survivor pays the award scaled by the fraction of its maximum HP the blow
   took off: 120 * 8 / 40 is 24.  A blow that missed scales the same award by a
   damage of 0 and so pays nothing at all -- and it still writes the global,
   which is what tells 0 apart from "not written". */
static void a_survivor_pays_the_award_in_proportion(void)
{
    stage();
    make_it_an_award_setup();
    set_hp(20, 40);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(data_fdps_battle_pending_xp_credit, 24);

    stage();
    make_it_an_award_setup();
    set_accuracy(0, 0);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(slot(OUTCOME_MISSED), 1);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, 0);
}

/* The enemy record is the portrait id less 0x3c, so portrait 0x3d resolves
   record 1 and not record 0: 10 * 30 / 5 is 60. */
static void the_enemy_record_is_the_portrait_id_less_0x3c(void)
{
    stage();
    make_it_an_award_setup();
    unit(DEFENDER)->portrait_id = FIRST_ENEMY_PORTRAIT + 1;
    set_hp(8, 40);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(data_fdps_battle_pending_xp_credit, 60);
}

/* An attacker whose portrait id is above 10 has 30 added to the level the award
   is divided by: 10 * 60 / 35 is 17 where the same blow from portrait 10 pays
   120.  The comparison is > 10 and not >= 10, so portrait 10 is unpenalised. */
static void a_portrait_id_above_ten_divides_by_thirty_more(void)
{
    stage();
    make_it_an_award_setup();
    unit(ATTACKER)->portrait_id = 11;
    set_hp(8, 40);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(data_fdps_battle_pending_xp_credit, 17);

    stage();
    make_it_an_award_setup();
    unit(ATTACKER)->portrait_id = 10;
    set_hp(8, 40);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(data_fdps_battle_pending_xp_credit, 120);
}

/* The guard is on the two SIDE bytes and on nothing else -- attacker 2 against
   defender 0.  This is where the function parts company with its map-side twin,
   which guards on the defender's PORTRAIT id instead: here a defender whose
   portrait is nowhere near the enemy base still earns the award as long as its
   side byte is 0, and one whose portrait is an enemy's earns nothing while its
   side is not.  Both are asserted, the second with a portrait that would have
   satisfied the twin. */
static void the_award_is_gated_on_the_two_side_bytes(void)
{
    stage();
    unit(DEFENDER)->portrait_id = FIRST_ENEMY_PORTRAIT;
    set_hp(8, 40);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SENTINEL);

    stage();
    unit(ATTACKER)->side = PLAYER_SIDE;
    unit(DEFENDER)->side = 1;
    unit(DEFENDER)->portrait_id = FIRST_ENEMY_PORTRAIT;
    set_hp(8, 40);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SENTINEL);

    stage();
    make_it_an_award_setup();
    set_hp(8, 40);
    fdps_combat_compute_hit_outcome(ATTACKER, DEFENDER, block());
    CHECK_EQ(data_fdps_battle_pending_xp_credit, 120);
}

/* Both indices select their own record.  The same duel run between units 2 and
   3 gives the same answer, and the records of units 0 and 1 are left alone. */
static void each_index_selects_its_own_record(void)
{
    stage();
    unit(2)->pos_x = 0;
    unit(2)->pos_y = 0;
    unit(2)->side = 1;
    unit(2)->ap = 100;
    unit(2)->hit = 200;
    unit(2)->level = 5;
    unit(2)->inventory_slots[0] = ENTRY_EQUIPPED;
    unit(2)->inventory_slots[1] = FIXTURE_WEAPON_ID;
    unit(3)->pos_x = 1;
    unit(3)->pos_y = 0;
    unit(3)->dp = 91;
    unit(3)->hp_current = 20;
    unit(3)->hp_max = 40;

    fdps_combat_compute_hit_outcome(2, 3, block());
    CHECK_EQ(slot(OUTCOME_MISSED), 0);
    CHECK_EQ(slot(OUTCOME_DAMAGE), 8);
    CHECK_EQ(unit(DEFENDER)->hp_current, 20);
    CHECK_EQ(unit(ATTACKER)->hp_current, 0);
}

/* ------------------------------------------------------------------ *
 * fdps_combat_slide_backdrops @ 0001a370
 *
 * Expected values come from the assembly: MOV dword ptr [EAX+0x10],0x18 at
 * 0001a37f for the y written once before the loop; CMP dword ptr [EBP-0x8],0x8
 * / JLE at 0001a38d for eight steps starting at 1; PUSH 0x16480 at 0001a3a0
 * for the page clear; IMUL EAX,[EBP-0x8],0x28 with IMUL by the direction and
 * ADD EDX,0x18 at 0001a3be for the outgoing x and the same with SUB EAX,0x140
 * at 0001a3e9 for the incoming one; the store order [EAX+0x18] then [EAX+0xc]
 * then [EAX+0x14] around each of the three CALLs to 0x14140; MOV EDX,[EAX+0x8]
 * and MOV EDX,[EAX] at 0001a41c for which two cursor slots the third draw
 * takes; the CALL to 0x14550 at 0001a446 sitting AFTER that draw; and the six
 * pushes at 0001a470 -- 0xc8, 0x140, 0x140, 0xa0000, 0x170, page + 0x2298 --
 * for the present.  None of them is read off the emitted C.
 *
 * WHAT THE RUN IS OBSERVED THROUGH.  Two surfaces: the caller's offscreen page,
 * which the fixture owns and reads back directly, and the VGA aperture, which
 * only answers in a graphics mode -- so each run puts the adapter into mode 13h
 * the way the game does, captures both, and returns to text mode afterwards.
 *
 * ONLY THE EIGHTH STEP IS VISIBLE ON THE PAGE, because every step memsets it
 * before drawing.  That is itself the strongest thing the page can say: a build
 * that dropped the clear, or ran a ninth step, or ran seven, leaves a different
 * count of painted bytes behind.  Where the backdrops sat on steps 1 through 7
 * has no unit observable and is a playtest contract.
 *
 * WHY A TIMER INTERRUPT IS INSTALLED.  Each step ends waiting for
 * data_fdps_timer_tick_counter to change, and in the game that counter is
 * advanced by the timer handler off AIL's interrupt.  Nothing advances it in a
 * test image, so the wait after the first step would never end.  Each run hooks
 * IRQ0 for the duration of the call with a handler that increments the counter
 * and chains to the one that was there.
 *
 * WHAT THE THREE .SAF FIXTURES ARE.  Three synthetic images, each one frame
 * deep except the animation's, each carrying a single 4x2 tile of its own
 * pixel value, so that the page says which of the three draws painted a byte
 * and not merely that something did.  The animation's layer carries an x offset
 * of 8 so its mark cannot land on the incoming backdrop's, which shares its x.
 * The .SAF layout is resource_info/saf.md; the offsets are restated here rather
 * than taken from src/sprite.c for the same reason the record offsets above
 * are.
 * ------------------------------------------------------------------ */

/* The geometry the two callers state and this function hard-codes. */
#define SLIDE_STEPS 8
#define SLIDE_STEP_PIXELS 0x28
#define SLIDE_MARGIN 0x18
#define SLIDE_PITCH 0x170
#define SLIDE_ROWS 0xf8
#define SLIDE_PAGE_BYTES 0x16480
#define SLIDE_WINDOW_AT 0x2298
#define SLIDE_SCREEN_W 0x140
#define SLIDE_SCREEN_H 0xc8
#define SLIDE_SCREEN_BYTES (SLIDE_SCREEN_W * SLIDE_SCREEN_H)
#define SLIDE_VGA_BASE 0x000a0000

/* The draw request's slots, as displacements off the request pointer:
   +0x00, +0x04, +0x08, +0x0c, +0x10, +0x14, +0x18 are the seven the assembly
   touches, and the block is nine dwords. */
#define SLIDE_REQ_BASE 0
#define SLIDE_REQ_PITCH 1
#define SLIDE_REQ_ROWS 2
#define SLIDE_REQ_X 3
#define SLIDE_REQ_Y 4
#define SLIDE_REQ_IMAGE 5
#define SLIDE_REQ_ITEM 6
#define SLIDE_REQ_OPERAND 7
#define SLIDE_REQ_MODE 8
#define SLIDE_REQ_DWORDS 9

/* The playback cursor's slots: [EAX] and [EAX+0x8] are the two the third draw
   reads, and the middle one is the tick count the advance keeps. */
#define SLIDE_CUR_FRAME 0
#define SLIDE_CUR_TICKS 1
#define SLIDE_CUR_IMAGE 2

/* The synthetic .SAF.  Header offsets first: the three magic bytes, the cell
   size, and the three section descriptors. */
#define SAF_CELL_W_AT 0x07
#define SAF_CELL_H_AT 0x09
#define SAF_FRAME_COUNT_AT 0x0c
#define SAF_FRAME_TABLE_PTR_AT 0x0e
#define SAF_TILEMAP_COUNT_AT 0x16
#define SAF_TILEMAP_TABLE_PTR_AT 0x18
#define SAF_TILE_COUNT_AT 0x20
#define SAF_TILE_TABLE_PTR_AT 0x22

/* Where the fixture puts each section.  The header ends at 0x34; one tile
   offset and its four-byte stream, one tilemap offset and its six-byte record,
   then the frame offset table and the single frame record every entry of it
   points at. */
#define SLIDE_TILE_TABLE_AT 0x34
#define SLIDE_TILE_STREAM_AT 0x38
#define SLIDE_TILEMAP_TABLE_AT 0x3c
#define SLIDE_TILEMAP_AT 0x40
#define SLIDE_FRAME_TABLE_AT 0x48
#define SLIDE_FRAME_AT 0x88
#define SLIDE_IMAGE_BYTES 0x100

/* A cell small enough that three of them fit across the page without meeting,
   and two rows tall so a draw that landed one row out is visible. */
#define SLIDE_CELL_W 4
#define SLIDE_CELL_H 2

/* The frame record: sound at +0, duration at +2, layer count at +8, the first
   layer at +0x0a; a layer is 13 bytes of tilemap number, x, y, blend flag and
   blend level. */
#define SLIDE_FRAME_SOUND_AT 0x00
#define SLIDE_FRAME_DURATION_AT 0x02
#define SLIDE_FRAME_LAYERS_AT 0x08
#define SLIDE_LAYER_AT 0x0a
#define SLIDE_LAYER_TILEMAP_AT 0x00
#define SLIDE_LAYER_X_AT 0x02
#define SLIDE_LAYER_Y_AT 0x04
#define SLIDE_LAYER_BLEND_AT 0x06

/* One pixel value per image, none of them zero, so a painted byte names the
   draw that wrote it. */
#define SLIDE_OUT_PIXEL 0xa1
#define SLIDE_IN_PIXEL 0xb2
#define SLIDE_ANIM_PIXEL 0xc3

/* The animation's layer sits 8 pixels right of its request, which is what
   keeps its mark clear of the incoming backdrop's at the same x. */
#define SLIDE_ANIM_LAYER_X 8

/* Sixteen frames of one tick each, so the cursor can be advanced eight times
   without reaching the end of the clip and wrapping. */
#define SLIDE_ANIM_FRAMES 16
#define SLIDE_ANIM_DURATION 1

/* Sentinels for the five request slots this function must not write. */
#define SLIDE_OPERAND_SENTINEL 0x5a5a5a5a
#define SLIDE_Y_SENTINEL (-999)
#define SLIDE_X_SENTINEL (-777)

/* What the aperture is filled with before each run, so that a byte still
   holding it is a byte the present never wrote. */
#define SLIDE_SCREEN_SENTINEL 0x5a

#define SLIDE_MODE_TEXT 0x03
#define SLIDE_MODE_320X200X256 0x13

/* IRQ0.  DOS/4GW reflects a hardware interrupt taken in protected mode to the
   protected-mode vector, so the handler installed here is the one that runs
   while the routine spins on the counter. */
#define SLIDE_TIMER_VECTOR 8

static unsigned char slide_out_image[SLIDE_IMAGE_BYTES];
static unsigned char slide_in_image[SLIDE_IMAGE_BYTES];
static unsigned char slide_anim_image[SLIDE_IMAGE_BYTES];
static unsigned char slide_page[SLIDE_PAGE_BYTES];
static unsigned char slide_screen[SLIDE_SCREEN_BYTES];
static int slide_request[SLIDE_REQ_DWORDS];
static int slide_cursor[3];

static void (__interrupt __far *slide_saved_timer)();

static void __interrupt __far slide_timer_isr(void)
{
    ++data_fdps_timer_tick_counter;
    _chain_intr(slide_saved_timer);
}

static void slide_u16(unsigned char *image, int at, unsigned int value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
}

static void slide_u32(unsigned char *image, int at, unsigned long value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
    image[at + 2] = (unsigned char) ((value >> 16) & 0xff);
    image[at + 3] = (unsigned char) ((value >> 24) & 0xff);
}

/* Builds one image: a 4x2 tile of `pixel`, a one-cell tilemap holding it, and
   `frames` frame-table entries all naming the one frame record, whose single
   layer sits at layer_x.  Command 0x03 is a fill run of four pixels
   (resource_info/cel.md), so two of them make the two rows of the cell. */
static void slide_stage_image(unsigned char *image, unsigned char pixel,
                              int frames, int duration, int layer_x)
{
    int index;

    memset(image, 0, SLIDE_IMAGE_BYTES);
    image[0] = 'S';
    image[1] = 'A';
    image[2] = 'F';
    slide_u16(image, SAF_CELL_W_AT, SLIDE_CELL_W);
    slide_u16(image, SAF_CELL_H_AT, SLIDE_CELL_H);

    slide_u16(image, SAF_TILE_COUNT_AT, 1);
    slide_u32(image, SAF_TILE_TABLE_PTR_AT, (unsigned long) SLIDE_TILE_TABLE_AT);
    slide_u32(image, SLIDE_TILE_TABLE_AT, (unsigned long) SLIDE_TILE_STREAM_AT);
    image[SLIDE_TILE_STREAM_AT] = 0x03;
    image[SLIDE_TILE_STREAM_AT + 1] = pixel;
    image[SLIDE_TILE_STREAM_AT + 2] = 0x03;
    image[SLIDE_TILE_STREAM_AT + 3] = pixel;

    slide_u16(image, SAF_TILEMAP_COUNT_AT, 1);
    slide_u32(image, SAF_TILEMAP_TABLE_PTR_AT,
              (unsigned long) SLIDE_TILEMAP_TABLE_AT);
    slide_u32(image, SLIDE_TILEMAP_TABLE_AT, (unsigned long) SLIDE_TILEMAP_AT);
    slide_u16(image, SLIDE_TILEMAP_AT, 1);
    slide_u16(image, SLIDE_TILEMAP_AT + 2, 1);
    slide_u16(image, SLIDE_TILEMAP_AT + 4, 0);

    slide_u16(image, SAF_FRAME_COUNT_AT, (unsigned int) frames);
    slide_u32(image, SAF_FRAME_TABLE_PTR_AT,
              (unsigned long) SLIDE_FRAME_TABLE_AT);
    for (index = 0; index < frames; index++) {
        slide_u32(image, SLIDE_FRAME_TABLE_AT + index * 4,
                  (unsigned long) SLIDE_FRAME_AT);
    }
    slide_u16(image, SLIDE_FRAME_AT + SLIDE_FRAME_SOUND_AT, 0xffff);
    slide_u16(image, SLIDE_FRAME_AT + SLIDE_FRAME_DURATION_AT,
              (unsigned int) duration);
    slide_u16(image, SLIDE_FRAME_AT + SLIDE_FRAME_LAYERS_AT, 1);
    slide_u16(image, SLIDE_FRAME_AT + SLIDE_LAYER_AT + SLIDE_LAYER_TILEMAP_AT,
              0);
    slide_u16(image, SLIDE_FRAME_AT + SLIDE_LAYER_AT + SLIDE_LAYER_X_AT,
              (unsigned int) layer_x);
    slide_u16(image, SLIDE_FRAME_AT + SLIDE_LAYER_AT + SLIDE_LAYER_Y_AT, 0);
    image[SLIDE_FRAME_AT + SLIDE_LAYER_AT + SLIDE_LAYER_BLEND_AT] = 0;
}

static void slide_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* One whole slide, with the adapter in the mode the game plays it in and a
   timer interrupt running, leaving the offscreen page in slide_page[], the
   visible window in slide_screen[], and the request and the cursor where the
   call left them. */
static void slide_run(int direction)
{
    slide_stage_image(slide_out_image, SLIDE_OUT_PIXEL, 1, 1, 0);
    slide_stage_image(slide_in_image, SLIDE_IN_PIXEL, 1, 1, 0);
    slide_stage_image(slide_anim_image, SLIDE_ANIM_PIXEL, SLIDE_ANIM_FRAMES,
                      SLIDE_ANIM_DURATION, SLIDE_ANIM_LAYER_X);

    memset(slide_page, 0, SLIDE_PAGE_BYTES);
    slide_request[SLIDE_REQ_BASE] = (int) slide_page;
    slide_request[SLIDE_REQ_PITCH] = SLIDE_PITCH;
    slide_request[SLIDE_REQ_ROWS] = SLIDE_ROWS;
    slide_request[SLIDE_REQ_X] = SLIDE_X_SENTINEL;
    slide_request[SLIDE_REQ_Y] = SLIDE_Y_SENTINEL;
    slide_request[SLIDE_REQ_IMAGE] = 0;
    slide_request[SLIDE_REQ_ITEM] = 0;
    slide_request[SLIDE_REQ_OPERAND] = SLIDE_OPERAND_SENTINEL;
    slide_request[SLIDE_REQ_MODE] = 0;

    slide_cursor[SLIDE_CUR_FRAME] = 0;
    slide_cursor[SLIDE_CUR_TICKS] = 0;
    slide_cursor[SLIDE_CUR_IMAGE] = (int) slide_anim_image;

    slide_set_mode(SLIDE_MODE_320X200X256);
    memset((void *) SLIDE_VGA_BASE, SLIDE_SCREEN_SENTINEL,
           (size_t) SLIDE_SCREEN_BYTES);

    slide_saved_timer = _dos_getvect(SLIDE_TIMER_VECTOR);
    _dos_setvect(SLIDE_TIMER_VECTOR, slide_timer_isr);
    fdps_combat_slide_backdrops(slide_out_image, slide_in_image, slide_cursor,
                                slide_request, direction);
    _dos_setvect(SLIDE_TIMER_VECTOR, slide_saved_timer);

    memmove(slide_screen, (void *) SLIDE_VGA_BASE,
            (size_t) SLIDE_SCREEN_BYTES);
    slide_set_mode(SLIDE_MODE_TEXT);
}

static int slide_page_pixel(int row, int col)
{
    return (int) slide_page[row * SLIDE_PITCH + col];
}

static int slide_screen_pixel(int row, int col)
{
    return (int) slide_screen[row * SLIDE_SCREEN_W + col];
}

/* How many bytes of the page hold a given value. */
static int slide_page_count(int value)
{
    long index;
    int found;

    found = 0;
    for (index = 0; index < (long) SLIDE_PAGE_BYTES; index++) {
        if ((int) slide_page[index] == value) {
            found++;
        }
    }
    return found;
}

/* How many bytes of the captured window hold a given value. */
static int slide_screen_count(int value)
{
    long index;
    int found;

    found = 0;
    for (index = 0; index < (long) SLIDE_SCREEN_BYTES; index++) {
        if ((int) slide_screen[index] == value) {
            found++;
        }
    }
    return found;
}

/* How many bytes of the page are not zero. */
static int slide_page_painted(void)
{
    long index;
    int found;

    found = 0;
    for (index = 0; index < (long) SLIDE_PAGE_BYTES; index++) {
        if (slide_page[index] != 0) {
            found++;
        }
    }
    return found;
}

/* The premise the page assertions rest on: three 4x2 cells and nothing else
   survive the run, so the clear at the top of every step really did run and
   exactly three draws really did paint.  Eight steps of leftovers would leave
   twenty-four times as much behind. */
static void only_the_last_of_the_eight_steps_is_left_on_the_page(void)
{
    slide_run(1);
    CHECK_EQ(slide_page_painted(),
             3 * SLIDE_CELL_W * SLIDE_CELL_H);
    CHECK_EQ(slide_page_count(SLIDE_OUT_PIXEL), SLIDE_CELL_W * SLIDE_CELL_H);
    CHECK_EQ(slide_page_count(SLIDE_IN_PIXEL), SLIDE_CELL_W * SLIDE_CELL_H);
    CHECK_EQ(slide_page_count(SLIDE_ANIM_PIXEL), SLIDE_CELL_W * SLIDE_CELL_H);
}

/* MOV dword ptr [EAX+0x10],0x18: the y is the page's border margin, is written
   before the loop, and is the row every draw lands on. */
static void the_y_is_the_pages_border_margin(void)
{
    slide_run(1);
    CHECK_EQ(slide_request[SLIDE_REQ_Y], SLIDE_MARGIN);
    CHECK_EQ(slide_page_pixel(SLIDE_MARGIN, SLIDE_MARGIN), SLIDE_IN_PIXEL);
    CHECK_EQ(slide_page_pixel(SLIDE_MARGIN + 1, SLIDE_MARGIN), SLIDE_IN_PIXEL);
    CHECK_EQ(slide_page_pixel(SLIDE_MARGIN - 1, SLIDE_MARGIN), 0);
    CHECK_EQ(slide_page_pixel(SLIDE_MARGIN + 2, SLIDE_MARGIN), 0);
}

/* IMUL 0x28 / SUB 0x140 / ADD 0x18 with step 8: the incoming backdrop's last x
   is 8 * 40 - 320 = 0, so it comes to rest on the border corner whichever way
   the slide ran, and the column in front of it is untouched. */
static void the_incoming_backdrop_comes_to_rest_on_the_corner(void)
{
    slide_run(1);
    CHECK_EQ(slide_page_pixel(SLIDE_MARGIN, SLIDE_MARGIN), SLIDE_IN_PIXEL);
    CHECK_EQ(slide_page_pixel(SLIDE_MARGIN, SLIDE_MARGIN - 1), 0);
    CHECK_EQ(slide_request[SLIDE_REQ_X], SLIDE_MARGIN);

    slide_run(-1);
    CHECK_EQ(slide_page_pixel(SLIDE_MARGIN, SLIDE_MARGIN), SLIDE_IN_PIXEL);
    CHECK_EQ(slide_request[SLIDE_REQ_X], SLIDE_MARGIN);
}

/* The outgoing backdrop's last x is 0x18 + direction * 8 * 40, one whole
   320-pixel screen from home.  Forwards that is column 344, still inside a
   368-byte row; backwards it is -296, which the cell drawer refuses, so the
   page holds none of its pixels at all. */
static void the_outgoing_backdrop_ends_one_screen_away(void)
{
    slide_run(1);
    CHECK_EQ(slide_page_pixel(SLIDE_MARGIN, SLIDE_MARGIN + SLIDE_SCREEN_W),
             SLIDE_OUT_PIXEL);
    CHECK_EQ(slide_page_pixel(SLIDE_MARGIN + 1,
                              SLIDE_MARGIN + SLIDE_SCREEN_W),
             SLIDE_OUT_PIXEL);
    CHECK_EQ(slide_page_count(SLIDE_OUT_PIXEL), SLIDE_CELL_W * SLIDE_CELL_H);

    slide_run(-1);
    CHECK_EQ(slide_page_count(SLIDE_OUT_PIXEL), 0);
    CHECK_EQ(slide_page_painted(), 2 * SLIDE_CELL_W * SLIDE_CELL_H);
}

/* Nothing writes the x between the second and the third draw, so the arriving
   sprite is composed at the incoming backdrop's x -- here that x plus the 8 the
   fixture's own layer carries. */
static void the_arriving_sprite_rides_at_the_incoming_backdrops_x(void)
{
    slide_run(1);
    CHECK_EQ(slide_page_pixel(SLIDE_MARGIN,
                              SLIDE_MARGIN + SLIDE_ANIM_LAYER_X),
             SLIDE_ANIM_PIXEL);
    CHECK_EQ(slide_page_pixel(SLIDE_MARGIN + 1,
                              SLIDE_MARGIN + SLIDE_ANIM_LAYER_X),
             SLIDE_ANIM_PIXEL);
}

/* The CALL to fdps_saf_advance_tick sits after the third draw, so step 8 draws
   frame 7 and the cursor is left on 8: one tick per step, and the frame drawn
   is always the one standing when the step began.  The clip is sixteen frames
   of one tick, so neither number is a wrap. */
static void the_cursor_is_advanced_once_per_step_after_the_draw(void)
{
    slide_run(1);
    CHECK_EQ(slide_cursor[SLIDE_CUR_FRAME], SLIDE_STEPS);
    CHECK_EQ(slide_cursor[SLIDE_CUR_TICKS], 0);
    CHECK_EQ(slide_request[SLIDE_REQ_ITEM], SLIDE_STEPS - 1);
    CHECK_EQ(slide_request[SLIDE_REQ_IMAGE], (int) slide_anim_image);
}

/* Only four of the request's slots are written.  The page pointer, the two
   extents and the two blend slots come back holding what the fixture put
   there, which is what lets the caller keep one block across the whole
   exchange. */
static void the_page_and_the_blend_slots_come_back_untouched(void)
{
    slide_run(1);
    CHECK_EQ(slide_request[SLIDE_REQ_BASE], (int) slide_page);
    CHECK_EQ(slide_request[SLIDE_REQ_PITCH], SLIDE_PITCH);
    CHECK_EQ(slide_request[SLIDE_REQ_ROWS], SLIDE_ROWS);
    CHECK_EQ(slide_request[SLIDE_REQ_OPERAND], SLIDE_OPERAND_SENTINEL);
    CHECK_EQ(slide_request[SLIDE_REQ_MODE], 0);
}

/* The present takes the 320 by 200 window at page + 0x2298 -- the border
   corner -- and lays it on the aperture with 320 bytes to the row out of a
   368-byte source row.  So the incoming backdrop's corner pixel is the screen's
   first byte, the sprite is 8 columns along, and the outgoing backdrop at page
   column 344 is past the window's right edge and does not appear at all. */
static void the_present_copies_the_window_at_the_border_corner(void)
{
    slide_run(1);
    CHECK_EQ(slide_screen_pixel(0, 0), SLIDE_IN_PIXEL);
    CHECK_EQ(slide_screen_pixel(1, 0), SLIDE_IN_PIXEL);
    CHECK_EQ(slide_screen_pixel(0, SLIDE_ANIM_LAYER_X), SLIDE_ANIM_PIXEL);
    CHECK_EQ(slide_screen_pixel(0, SLIDE_SCREEN_W - 1), 0);
    CHECK_EQ(slide_screen_pixel(2, 0), 0);
    CHECK_EQ(slide_screen_pixel(SLIDE_SCREEN_H - 1, SLIDE_SCREEN_W - 1), 0);
    /* Every byte of the window was written: none of the fill survives. */
    CHECK_EQ(slide_screen_count(SLIDE_SCREEN_SENTINEL), 0);
    /* And none of it came from beyond the window's right edge: the outgoing
       backdrop sits at page column 344 and a transfer that took 368 bytes to
       the row instead of 320 would have carried it in. */
    CHECK_EQ(slide_screen_count(SLIDE_OUT_PIXEL), 0);
    CHECK_EQ(slide_screen_count(SLIDE_IN_PIXEL), SLIDE_CELL_W * SLIDE_CELL_H);
}


/* ------------------------------------------------------------------ *
 * fdps_combat_slide_in_attacker @ 000194e0
 *
 * The entrance runs on the same offscreen page and the same presenter as the
 * slide above, so the .SAF fixtures, the mode switch, the aperture capture and
 * the IRQ0 handler that lets the frame wait finish are the ones built for it.
 * Two things are new.
 *
 * THE PAGE IS THE FUNCTION'S OWN.  It comes from malloc at 000194f1 and is
 * freed at 000196b2, so no fixture can look at it and every position assertion
 * below reads the aperture instead.  The present copies page (24,24) to screen
 * (0,0), so a page coordinate is a screen coordinate less the 24-pixel apron.
 *
 * ONLY THE LAST OF THE NINE FRAMES SURVIVES THE RUN, and the last frame is by
 * construction the one with everything at rest, so the aperture on its own
 * cannot say which way anything travelled.  The motion is sampled instead:
 * walkin_timer_isr scans the top row of the aperture on every tick and records
 * where the attacker's mark was, which is exactly the moment the routine is
 * spinning on the counter with a finished frame on screen.  A sample is kept
 * only once the row holds a zero byte, because before the first present the
 * aperture is still full of the fixture's sentinel.
 *
 * The gauge art both panels are painted from is staged rather than loaded: a
 * four-sprite .CEL of one pixel each, so a frame blit marks its own corner and
 * nothing else, and a flat fill sheet.  Ticket 23 owns what either real sheet
 * holds and neither is opened by name here.
 * ------------------------------------------------------------------ */

/* Nine frames, 8 down to 0: MOV dword ptr [EBP-0x10],0x8 at 0001954e with
   CMP ...,0x0 / JGE at 00019555.  WALKIN_FIRST_FRAME is the counter's first
   value and WALKIN_FRAMES how many passes that makes. */
#define WALKIN_FIRST_FRAME 8
#define WALKIN_FRAMES 9

/* IMUL EAX,dword ptr [EBP-0x10],0x14 at 0001958f and MOV dword ptr
   [EBP-0xc],0xf / 0xfffffff1 at 0001953e and 00019547. */
#define WALKIN_BACKDROP_RISE 0x14
#define WALKIN_SLIDE 0x0f

/* One pixel value per image, none of them zero and none of them the slide
   fixture's, so a painted byte names the draw that wrote it. */
#define WALKIN_BACKDROP_PIXEL 0x11
#define WALKIN_ATTACKER_PIXEL 0x22
#define WALKIN_DEFENDER_PIXEL 0x33

/* The two combatants' layers sit 8 and 16 pixels right of their request, so
   the three marks of a resting frame cannot land on one another. */
#define WALKIN_ATTACKER_LAYER_X 8
#define WALKIN_DEFENDER_LAYER_X 16

/* The two gauge panels' origins in page coordinates, from the assembly of
   fdps_draw_unit_hp_mp_gauges at 00019310: SHL EAX,0x5 / ADD EAX,0xc3 for the
   upper right, IMUL EAX,...,0xc7 / ADD EAX,0x1e for the lower left, and PUSH
   0xa for the MP frame's row.  A side of 0 takes the lower left. */
#define WALKIN_UP_RIGHT_ROW 0x20
#define WALKIN_UP_RIGHT_COLUMN 0xc3
#define WALKIN_LOW_LEFT_ROW 0xc7
#define WALKIN_LOW_LEFT_COLUMN 0x1e
#define WALKIN_MP_FRAME_ROW 10

/* Four one-pixel sprites: 0 and 1 are the upper-right panel's two frames and
   2 and 3 the lower left's.  The header is fifteen bytes and the offset table
   is sprite_count + 1 four-byte entries measured from the start of the sheet
   (resource_info/cel.md). */
#define WALKIN_CEL_SPRITES 4
#define WALKIN_CEL_TABLE_START 15
#define WALKIN_CEL_STREAM_START (WALKIN_CEL_TABLE_START \
                                 + (WALKIN_CEL_SPRITES + 1) * 4)
#define WALKIN_CEL_STREAM_BYTES 2
#define WALKIN_CEL_BYTES (WALKIN_CEL_STREAM_START \
                          + WALKIN_CEL_SPRITES * WALKIN_CEL_STREAM_BYTES)
#define WALKIN_CEL_PIXEL_BASE 0x80

/* Four fill strips of 0x271 bytes, the packed surface src/gauge.c describes. */
#define WALKIN_FILL_BYTES 0x9c4
#define WALKIN_FILL_PIXEL 0x40

/* Room for more samples than nine frames can produce, so an overflow cannot
   silently truncate the record. */
#define WALKIN_MAX_SAMPLES 128

/* What a sample holds when the attacker's mark was not on the top row at all. */
#define WALKIN_ABSENT (-1)

static unsigned char walkin_backdrop_image[SLIDE_IMAGE_BYTES];
static unsigned char walkin_attacker_image[SLIDE_IMAGE_BYTES];
static unsigned char walkin_defender_image[SLIDE_IMAGE_BYTES];
static unsigned char walkin_cel[WALKIN_CEL_BYTES];
static unsigned char walkin_fill[WALKIN_FILL_BYTES];
static unsigned char walkin_screen[SLIDE_SCREEN_BYTES];
static int walkin_attacker_cursor[3];
static int walkin_defender_cursor[3];
static int walkin_samples[WALKIN_MAX_SAMPLES];
static int walkin_sample_count;

static void (__interrupt __far *walkin_saved_timer)();

/* Drives the counter the frame wait spins on, and takes one reading of the
   finished frame while it is still on screen. */
static void __interrupt __far walkin_timer_isr(void)
{
    unsigned char *aperture;
    int column;
    int found;
    int presented;

    if (walkin_sample_count < WALKIN_MAX_SAMPLES) {
        aperture = (unsigned char *) SLIDE_VGA_BASE;
        found = WALKIN_ABSENT;
        presented = 0;
        for (column = 0; column < SLIDE_SCREEN_W; column++) {
            if (aperture[column] == 0) {
                presented = 1;
            } else if (aperture[column] == WALKIN_ATTACKER_PIXEL
                       && found == WALKIN_ABSENT) {
                found = column;
            }
        }
        if (presented != 0) {
            walkin_samples[walkin_sample_count] = found;
            walkin_sample_count++;
        }
    }

    ++data_fdps_timer_tick_counter;
    _chain_intr(walkin_saved_timer);
}

/* One whole entrance, with the adapter in the mode the game plays it in and a
   timer interrupt running, leaving the presented window in walkin_screen[],
   the two cursors where the call left them and the top-row readings in
   walkin_samples[]. */
static void walkin_run(int attacker_side, int attacker_only)
{
    int offset;
    int sprite;

    stage();

    unit(ATTACKER)->side = (unsigned char) attacker_side;
    unit(ATTACKER)->hp_current = 50;
    unit(ATTACKER)->hp_max = 100;
    unit(ATTACKER)->mp_current = 25;
    unit(ATTACKER)->mp_max = 100;

    unit(DEFENDER)->side = ENEMY_SIDE;
    unit(DEFENDER)->hp_current = 50;
    unit(DEFENDER)->hp_max = 100;
    unit(DEFENDER)->mp_current = 25;
    unit(DEFENDER)->mp_max = 100;

    for (offset = 0; offset < WALKIN_CEL_BYTES; offset++) {
        walkin_cel[offset] = 0;
    }
    walkin_cel[0] = 'C';
    walkin_cel[1] = 'E';
    walkin_cel[2] = 'L';
    *(short *) (walkin_cel + 7) = 1;
    *(short *) (walkin_cel + 9) = 1;
    *(short *) (walkin_cel + 11) = WALKIN_CEL_SPRITES;
    for (sprite = 0; sprite < WALKIN_CEL_SPRITES; sprite++) {
        offset = WALKIN_CEL_STREAM_START + sprite * WALKIN_CEL_STREAM_BYTES;
        *(int *) (walkin_cel + WALKIN_CEL_TABLE_START + sprite * 4) = offset;
        /* Command 0x00 is a fill run of one pixel and the byte after it is the
           pixel (rle.h). */
        walkin_cel[offset] = 0x00;
        walkin_cel[offset + 1] = (unsigned char) (WALKIN_CEL_PIXEL_BASE
                                                  + sprite);
    }
    *(int *) (walkin_cel + WALKIN_CEL_TABLE_START + WALKIN_CEL_SPRITES * 4) =
        WALKIN_CEL_BYTES;
    data_fdps_combat_gauge_sprite_sheet_ptr = walkin_cel;

    for (offset = 0; offset < WALKIN_FILL_BYTES; offset++) {
        walkin_fill[offset] = WALKIN_FILL_PIXEL;
    }
    data_fdps_gauge_fill_sheet_ptr = walkin_fill;

    slide_stage_image(walkin_backdrop_image, WALKIN_BACKDROP_PIXEL, 1, 1, 0);
    slide_stage_image(walkin_attacker_image, WALKIN_ATTACKER_PIXEL,
                      SLIDE_ANIM_FRAMES, SLIDE_ANIM_DURATION,
                      WALKIN_ATTACKER_LAYER_X);
    slide_stage_image(walkin_defender_image, WALKIN_DEFENDER_PIXEL,
                      SLIDE_ANIM_FRAMES, SLIDE_ANIM_DURATION,
                      WALKIN_DEFENDER_LAYER_X);

    walkin_attacker_cursor[SLIDE_CUR_FRAME] = 0;
    walkin_attacker_cursor[SLIDE_CUR_TICKS] = 0;
    walkin_attacker_cursor[SLIDE_CUR_IMAGE] = (int) walkin_attacker_image;
    walkin_defender_cursor[SLIDE_CUR_FRAME] = 0;
    walkin_defender_cursor[SLIDE_CUR_TICKS] = 0;
    walkin_defender_cursor[SLIDE_CUR_IMAGE] = (int) walkin_defender_image;

    walkin_sample_count = 0;
    for (offset = 0; offset < WALKIN_MAX_SAMPLES; offset++) {
        walkin_samples[offset] = WALKIN_ABSENT;
    }

    slide_set_mode(SLIDE_MODE_320X200X256);
    memset((void *) SLIDE_VGA_BASE, SLIDE_SCREEN_SENTINEL,
           (size_t) SLIDE_SCREEN_BYTES);

    walkin_saved_timer = _dos_getvect(SLIDE_TIMER_VECTOR);
    _dos_setvect(SLIDE_TIMER_VECTOR, walkin_timer_isr);
    fdps_combat_slide_in_attacker(ATTACKER, DEFENDER, attacker_only,
                                  walkin_attacker_cursor,
                                  walkin_defender_cursor,
                                  walkin_backdrop_image);
    _dos_setvect(SLIDE_TIMER_VECTOR, walkin_saved_timer);

    memmove(walkin_screen, (void *) SLIDE_VGA_BASE,
            (size_t) SLIDE_SCREEN_BYTES);
    slide_set_mode(SLIDE_MODE_TEXT);
}

/* One byte of the presented window. */
static int walkin_screen_pixel(int row, int column)
{
    return (int) walkin_screen[row * SLIDE_SCREEN_W + column];
}

/* And one byte of it addressed in page coordinates, which is how every
   position in the assembly is written. */
static int walkin_page_pixel(int page_row, int page_column)
{
    return walkin_screen_pixel(page_row - SLIDE_MARGIN,
                               page_column - SLIDE_MARGIN);
}

/* How many readings did not find the attacker on the top row at all. */
static int walkin_absent_samples(void)
{
    int index;
    int absent;

    absent = 0;
    for (index = 0; index < walkin_sample_count; index++) {
        if (walkin_samples[index] == WALKIN_ABSENT) {
            absent++;
        }
    }
    return absent;
}

/* The rightmost column any reading found him at, or WALKIN_ABSENT if none
   did. */
static int walkin_rightmost_sample(void)
{
    int index;
    int rightmost;

    rightmost = WALKIN_ABSENT;
    for (index = 0; index < walkin_sample_count; index++) {
        if (walkin_samples[index] > rightmost) {
            rightmost = walkin_samples[index];
        }
    }
    return rightmost;
}

/* How many readings found him somewhere that is not a whole number of 15-column
   steps out from his resting place, which is where every frame of the entrance
   must put him. */
static int walkin_off_grid_samples(void)
{
    int index;
    int column;
    int wrong;

    wrong = 0;
    for (index = 0; index < walkin_sample_count; index++) {
        column = walkin_samples[index];
        if (column != WALKIN_ABSENT) {
            if (column < WALKIN_ATTACKER_LAYER_X
                || column > WALKIN_FIRST_FRAME * WALKIN_SLIDE
                            + WALKIN_ATTACKER_LAYER_X
                || (column - WALKIN_ATTACKER_LAYER_X) % WALKIN_SLIDE != 0) {
                wrong++;
            }
        }
    }
    return wrong;
}

/* CMP byte ptr [EAX+0x6],0x0 / JZ at 00019538: the direction is decided by the
   record's side byte and by nothing else, so it is that offset the whole test
   rests on. */
static void the_direction_is_taken_from_record_offset_six(void)
{
    CHECK_EQ((int) offsetof(struct fdps_unit_record, side), 6);
}

/* Nine passes, one advance each, on clips of sixteen frames so that neither
   count is a wrap.  The advance is BEFORE the draw here -- the CALLs at
   000195b5 and 00019600 come ahead of the slot reads at 000195c4 and 00019608
   -- so frame 0 of each clip is never shown and the cursors are left on 9. */
static void nine_frames_advance_both_cursors_nine_times(void)
{
    walkin_run(2, 0);
    CHECK_EQ(walkin_attacker_cursor[SLIDE_CUR_FRAME], WALKIN_FRAMES);
    CHECK_EQ(walkin_attacker_cursor[SLIDE_CUR_TICKS], 0);
    CHECK_EQ(walkin_defender_cursor[SLIDE_CUR_FRAME], WALKIN_FRAMES);
    CHECK_EQ(walkin_defender_cursor[SLIDE_CUR_TICKS], 0);
}

/* CMP dword ptr [EBP+0x1c],0x0 / JNZ at 000195a8 jumps the whole defender
   block, so his cursor is not advanced, his sprite is not drawn and his panel
   is not painted.  The attacker's nine advances still happen. */
static void attacker_only_leaves_the_defender_out_entirely(void)
{
    walkin_run(2, 1);
    CHECK_EQ(walkin_defender_cursor[SLIDE_CUR_FRAME], 0);
    CHECK_EQ(walkin_attacker_cursor[SLIDE_CUR_FRAME], WALKIN_FRAMES);
    CHECK_EQ(walkin_screen_pixel(0, WALKIN_DEFENDER_LAYER_X), 0);
    CHECK_EQ(walkin_page_pixel(WALKIN_LOW_LEFT_ROW, WALKIN_LOW_LEFT_COLUMN), 0);
    CHECK_EQ(walkin_page_pixel(WALKIN_LOW_LEFT_ROW + WALKIN_MP_FRAME_ROW,
                               WALKIN_LOW_LEFT_COLUMN), 0);
}

/* At a counter of 0 every position is its own margin: the backdrop's y is
   0 * 0x14 + 0x18 and the attacker's x is 0 * step + 0x18, so all three marks
   land on the border corner and the present puts that corner at screen (0,0).
   The cells are 4 by 2, so row 2 is clear. */
static void the_last_frame_has_everything_on_the_border_corner(void)
{
    walkin_run(2, 0);
    CHECK_EQ(walkin_screen_pixel(0, 0), WALKIN_BACKDROP_PIXEL);
    CHECK_EQ(walkin_screen_pixel(1, 0), WALKIN_BACKDROP_PIXEL);
    CHECK_EQ(walkin_screen_pixel(0, WALKIN_ATTACKER_LAYER_X),
             WALKIN_ATTACKER_PIXEL);
    CHECK_EQ(walkin_screen_pixel(1, WALKIN_ATTACKER_LAYER_X),
             WALKIN_ATTACKER_PIXEL);
    CHECK_EQ(walkin_screen_pixel(0, WALKIN_DEFENDER_LAYER_X),
             WALKIN_DEFENDER_PIXEL);
    CHECK_EQ(walkin_screen_pixel(2, 0), 0);
    /* Every byte of the window was written, so none of the fixture's fill
       survived the nine presents. */
    CHECK_EQ(walkin_screen_pixel(SLIDE_SCREEN_H - 1, SLIDE_SCREEN_W - 1), 0);
}

/* Both bars are painted every frame, each into the panel its own side byte
   picks: the side-2 attacker into the upper right out of sprites 0 and 1, the
   side-0 defender into the lower left out of sprites 2 and 3.  The MP frame is
   ten rows under the HP frame in both. */
static void each_combatant_gets_the_panel_his_side_byte_picks(void)
{
    walkin_run(2, 0);
    CHECK_EQ(walkin_page_pixel(WALKIN_UP_RIGHT_ROW, WALKIN_UP_RIGHT_COLUMN),
             WALKIN_CEL_PIXEL_BASE);
    CHECK_EQ(walkin_page_pixel(WALKIN_UP_RIGHT_ROW + WALKIN_MP_FRAME_ROW,
                               WALKIN_UP_RIGHT_COLUMN),
             WALKIN_CEL_PIXEL_BASE + 1);
    CHECK_EQ(walkin_page_pixel(WALKIN_LOW_LEFT_ROW, WALKIN_LOW_LEFT_COLUMN),
             WALKIN_CEL_PIXEL_BASE + 2);
    CHECK_EQ(walkin_page_pixel(WALKIN_LOW_LEFT_ROW + WALKIN_MP_FRAME_ROW,
                               WALKIN_LOW_LEFT_COLUMN),
             WALKIN_CEL_PIXEL_BASE + 3);
}

/* MOV dword ptr [EBP-0xc],0xf at 0001953e is the arm a side other than 0
   takes, so the attacker starts 8 * 15 columns to the RIGHT of home and closes
   in.  Every reading finds him, and every one finds him a whole number of
   15-column steps out; at least one finds him short of home, which is what says
   he moved at all. */
static void a_side_two_attacker_closes_in_from_the_right(void)
{
    walkin_run(2, 0);
    CHECK_EQ(walkin_sample_count >= 2, 1);
    CHECK_EQ(walkin_absent_samples(), 0);
    CHECK_EQ(walkin_off_grid_samples(), 0);
    CHECK_EQ(walkin_rightmost_sample() > WALKIN_ATTACKER_LAYER_X, 1);
    CHECK_EQ(walkin_screen_pixel(0, WALKIN_ATTACKER_LAYER_X),
             WALKIN_ATTACKER_PIXEL);
}

/* MOV dword ptr [EBP-0xc],0xfffffff1 at 00019547 is the arm side 0 takes, so
   the same eight steps run the other way and put him at page column -96 on the
   first frame -- off the left of the window entirely.  So no reading finds him
   anywhere right of home, and some find him nowhere at all, while the last
   frame still stands him on the corner. */
static void a_side_zero_attacker_closes_in_from_the_left(void)
{
    walkin_run(0, 0);
    CHECK_EQ(walkin_sample_count >= 2, 1);
    CHECK_EQ(walkin_rightmost_sample(), WALKIN_ATTACKER_LAYER_X);
    CHECK_EQ(walkin_absent_samples() > 0, 1);
    CHECK_EQ(walkin_screen_pixel(0, WALKIN_ATTACKER_LAYER_X),
             WALKIN_ATTACKER_PIXEL);
}

/* The one the rebuild note is about.  With the defender drawn, his own store of
   0x18 at 000195bd is the last thing to touch the request's y, so the attacker
   is on the top row every frame -- which the case above already leans on, every
   reading finding him there.  With attacker_only set that store never runs and
   the y is still the backdrop's climbing 0x14 * counter + 0x18, so he is 160
   rows down on the first frame and only reaches the top row at the end: the
   readings lose him.  An emitter that filled the request in completely before
   each draw would make these two runs identical. */
static void attacker_only_lets_the_attacker_ride_the_backdrop_down(void)
{
    walkin_run(2, 1);
    CHECK_EQ(walkin_sample_count >= 2, 1);
    CHECK_EQ(walkin_absent_samples() > 0, 1);
    CHECK_EQ(walkin_screen_pixel(0, WALKIN_ATTACKER_LAYER_X),
             WALKIN_ATTACKER_PIXEL);
}

/* ---- fdps_combat_play_attack_exchange @ 00018d60 -------------------------
 *
 * This one is run whole, against the real game files, because there is nothing
 * smaller to run: the function is a sequencer whose every step is a call, and
 * the only things a caller can see are the globals it leaves behind, the
 * screen, the DAC and the two records.  So each case below puts the adapter
 * into mode 13h the way the game does, hooks IRQ0 so the presenters' tick waits
 * end, and calls it with FMER1.TMP, FMER2.TMP, MER1.TMP, MER2.TMP, MISC.VFS,
 * FIGHT.VFS, FIGACT.VFS and BACKGRND.VFS staged beside the executable
 * (tests/gamefile.lst).  A fabricated stand-in would prove nothing: every one
 * of the seven member names is composed inside the function out of a record
 * byte, so what is asserted is that the composed name found the member the
 * assembly's format string names.
 *
 * WHAT THE TIMER INTERRUPT IS FOR BESIDES THE TICK.  Four of this function's
 * effects only exist while it is running -- the fight blend tables, the gauge
 * fill sheet it frees before returning, the fight palette, and the backdrop id
 * the terrain images were named from, which every blow then republishes over --
 * so the handler takes one snapshot of all four the first tick after the first
 * frame reaches the screen.  That moment is inside fdps_combat_slide_in_attacker: past every
 * load, past the four gauge decodes and past the fight palette upload, and
 * before fdps_combat_play_blow's critical flash can touch the DAC.  The
 * aperture is filled with a sentinel before the call, so "a frame has reached
 * the screen" is a read of video memory and needs no port of its own.
 *
 * WHY ONE BASELINE RUN IS CACHED.  A run plays two whole animations paced by
 * the timer, so the five cases that read the standard duel share one, and
 * everything each of them asserts is copied out of the globals the moment it
 * returns.  The five cases that need a different setup each run their own.
 *
 * THE EXPECTED VALUES.  The two clip ids are chosen off the shipped files:
 * ACT000.SAF's frame 0 carries a lead-in count of 0 and ACT005.SAF's carries
 * 19, which is what separates the standing attack from the travelling one.  The
 * backdrop ids are staged into the two tiles' attribute rows, so what the
 * global holds afterwards says which tile was resolved last and that the id was
 * decremented.  Everything else comes off the assembly cited in src/combat.c.
 */

#define EXCH_TIMER_VECTOR 8
#define EXCH_MODE_TEXT 0x03
#define EXCH_MODE_320X200X256 0x13
#define EXCH_VGA_BASE 0x000a0000
#define EXCH_SCREEN_BYTES (0x140 * 0xc8)

/* What the aperture is filled with before a run.  A probe every 4001 bytes is
   sixteen readings spread over the whole window, so the first frame's blit
   cannot leave all of them looking untouched. */
#define EXCH_SCREEN_SENTINEL 0x5a
#define EXCH_PROBE_STRIDE 4001

#define EXCH_DAC_READ_INDEX 0x3c7
#define EXCH_DAC_WRITE_INDEX 0x3c8
#define EXCH_DAC_DATA 0x3c9
#define EXCH_DAC_ENTRIES 256
/* Outside the 0..60 span both staged palettes are built in, so a DAC entry
   still holding it is one neither upload wrote. */
#define EXCH_DAC_SENTINEL 63
#define EXCH_PALETTE_SPAN 61
#define EXCH_READING_SLOTS 6

/* PUSH 0x4800 at 00018d9f and PUSH 0x1000 at 00018ddb.  The window compared is
   128 bytes at an offset where the fight file and the map file differ:
   FMER1.TMP and MER1.TMP first disagree at 1028, FMER2.TMP and MER2.TMP at 2. */
#define EXCH_RAMP_BYTES 0x4800
#define EXCH_CUBE_BYTES 0x1000
#define EXCH_RAMP_WINDOW_AT 1024
#define EXCH_CUBE_WINDOW_AT 0
#define EXCH_WINDOW_BYTES 128

/* PUSH 0x9c4 at 00018e15, then sprites 4..7 at pitch 0x7d and y = index * 5. */
#define EXCH_FILL_BYTES 0x9c4
#define EXCH_FILL_STRIPS 4
#define EXCH_FILL_FIRST_SPRITE 4
#define EXCH_FILL_PITCH 0x7d
#define EXCH_FILL_STRIP_HEIGHT 5
/* The sprite index a body that started the run at 0 would have used. */
#define EXCH_FILL_WRONG_SPRITE 0

/* ACT000.SAF's frame 0 has a lead-in of 0 and ACT005.SAF's has 19, and both
   have a STAND%03d.SAF beside them.  The defender is given a third id so that
   a clip loaded for the wrong combatant would be a different file. */
#define EXCH_STANDING_PORTRAIT 0
#define EXCH_TRAVELLING_PORTRAIT 5
#define EXCH_DEFENDER_PORTRAIT 1

/* The two tiles' combat_backdrop_id bytes.  Both are above zero, so both are
   decremented, and they differ, so the survivor names the tile. */
#define EXCH_ATTACKER_BACKDROP_ID 7
#define EXCH_DEFENDER_BACKDROP_ID 5

/* Byte +3 of a four-byte attribute row is combat_backdrop_id. */
#define EXCH_ATTR_BACKDROP_AT 3

/* Both combatants start level with each other: AP 100 against DP 91 is a gap
   of 9 and so a damage of 8, and accuracy 200 against evasion 0 lands on every
   draw, so each blow that is played takes HP off and a blow that was not
   played leaves the record standing at its start. */
#define EXCH_START_HP 60
#define EXCH_FIXTURE_ATTACK 100
#define EXCH_FIXTURE_DEFENSE 91
#define EXCH_FIXTURE_ACCURACY 200
/* Less than one blow's damage, so the first blow floors the record at zero and
   fdps_combat_play_blow answers 0. */
#define EXCH_FATAL_HP 4

static struct fdps_palette_entry exch_fight_palette[EXCH_DAC_ENTRIES];
static struct fdps_palette_entry exch_map_palette[EXCH_DAC_ENTRIES];

static unsigned char exch_fill_snapshot[EXCH_FILL_BYTES];
static unsigned char exch_fill_over_zero[EXCH_FILL_BYTES];
static unsigned char exch_fill_over_ones[EXCH_FILL_BYTES];
static unsigned char exch_ramp_snapshot[EXCH_WINDOW_BYTES];
static unsigned char exch_cube_snapshot[EXCH_WINDOW_BYTES];
static unsigned char exch_ramp_at_return[EXCH_WINDOW_BYTES];
static unsigned char exch_cube_at_return[EXCH_WINDOW_BYTES];
static unsigned char exch_file_window[EXCH_WINDOW_BYTES];

/* The member name is handed to fdps_vfs_load_entry, which upper-cases the
   caller's own storage in place, so it cannot be a literal the compiler may
   put anywhere read-only. */
static char exch_gauge_member[] = "FigBar.cel";
static char exch_misc_archive[] = "MISC.VFS";

static int exch_snapshot_dac[EXCH_READING_SLOTS];
static int exch_return_dac[EXCH_READING_SLOTS];
static int exch_snapshot_taken;
static int exch_snapshot_backdrop_id;
static int exch_screen_nonzero;
static int exch_attacker_hp;
static int exch_defender_hp;
static int exch_xp_credit;
static int exch_defender_paralysis;
static int exch_baseline_done;

static void (__interrupt __far *exch_saved_timer)();

static void exch_read_dac(int *into)
{
    outp(EXCH_DAC_READ_INDEX, 0);
    into[0] = (int) inp(EXCH_DAC_DATA);
    into[1] = (int) inp(EXCH_DAC_DATA);
    into[2] = (int) inp(EXCH_DAC_DATA);
    outp(EXCH_DAC_READ_INDEX, EXCH_DAC_ENTRIES - 1);
    into[3] = (int) inp(EXCH_DAC_DATA);
    into[4] = (int) inp(EXCH_DAC_DATA);
    into[5] = (int) inp(EXCH_DAC_DATA);
}

/* The three things that only exist while the exchange is running. */
static void exch_take_snapshot(void)
{
    memmove(exch_fill_snapshot, data_fdps_gauge_fill_sheet_ptr,
            (size_t) EXCH_FILL_BYTES);
    memmove(exch_ramp_snapshot,
            (unsigned char *) data_fdps_palette_shade_ramp_table
            + EXCH_RAMP_WINDOW_AT, (size_t) EXCH_WINDOW_BYTES);
    memmove(exch_cube_snapshot,
            data_fdps_inverse_palette_cube + EXCH_CUBE_WINDOW_AT,
            (size_t) EXCH_WINDOW_BYTES);
    exch_snapshot_backdrop_id = (int) data_fdps_map_tile_combat_backdrop_id;
    exch_read_dac(exch_snapshot_dac);
}

static void __interrupt __far exch_timer_isr(void)
{
    unsigned char *aperture;
    int probe;

    ++data_fdps_timer_tick_counter;

    if (exch_snapshot_taken == 0) {
        aperture = (unsigned char *) EXCH_VGA_BASE;
        for (probe = 0; probe < EXCH_SCREEN_BYTES; probe += EXCH_PROBE_STRIDE) {
            if (aperture[probe] != EXCH_SCREEN_SENTINEL) {
                exch_take_snapshot();
                exch_snapshot_taken = 1;
                break;
            }
        }
    }

    _chain_intr(exch_saved_timer);
}

static void exch_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* The combat_backdrop_id byte of each combatant's own tile: the attacker
   stands on cell (0,0) and so on attribute row 0, the defender on (1,0) and so
   on row 1. */
static void exch_set_backdrop_ids(int attacker_id, int defender_id)
{
    stage_attr[ATTR_ROWS_AT + ATTACKER_ATTR_ROW * 4 + EXCH_ATTR_BACKDROP_AT] =
        (unsigned char) attacker_id;
    stage_attr[ATTR_ROWS_AT + DEFENDER_ATTR_ROW * 4 + EXCH_ATTR_BACKDROP_AT] =
        (unsigned char) defender_id;
}

/* The standard duel, on top of the fixture the outcome cases share: both units
   able to hurt each other for 8, the defender armed with a range-1 weapon so
   the counterattack test passes, and two palettes whose entries no other
   palette in the run can be confused with. */
static void exch_stage(int attacker_portrait)
{
    int index;

    stage();

    unit(ATTACKER)->portrait_id = (unsigned char) attacker_portrait;
    unit(ATTACKER)->dp = EXCH_FIXTURE_DEFENSE;
    unit(ATTACKER)->ev = 0;
    unit(ATTACKER)->hp_current = EXCH_START_HP;
    unit(ATTACKER)->hp_max = EXCH_START_HP;

    unit(DEFENDER)->portrait_id = EXCH_DEFENDER_PORTRAIT;
    unit(DEFENDER)->ap = EXCH_FIXTURE_ATTACK;
    unit(DEFENDER)->hit = EXCH_FIXTURE_ACCURACY;
    unit(DEFENDER)->hp_current = EXCH_START_HP;
    unit(DEFENDER)->hp_max = EXCH_START_HP;
    unit(DEFENDER)->inventory_slots[0] = ENTRY_EQUIPPED;
    unit(DEFENDER)->inventory_slots[1] = FIXTURE_WEAPON_ID;

    item(FIXTURE_WEAPON_ID)->range_min = 1;

    exch_set_backdrop_ids(EXCH_ATTACKER_BACKDROP_ID, EXCH_DEFENDER_BACKDROP_ID);

    for (index = 0; index < EXCH_DAC_ENTRIES; index++) {
        exch_fight_palette[index].red =
            (unsigned char) (index % EXCH_PALETTE_SPAN);
        exch_fight_palette[index].green =
            (unsigned char) ((index + 7) % EXCH_PALETTE_SPAN);
        exch_fight_palette[index].blue =
            (unsigned char) ((index + 14) % EXCH_PALETTE_SPAN);
        exch_map_palette[index].red =
            (unsigned char) ((index + 21) % EXCH_PALETTE_SPAN);
        exch_map_palette[index].green =
            (unsigned char) ((index + 28) % EXCH_PALETTE_SPAN);
        exch_map_palette[index].blue =
            (unsigned char) ((index + 35) % EXCH_PALETTE_SPAN);
    }
    data_fdps_vga_fight_palette_ptr = (unsigned char *) exch_fight_palette;
    data_fdps_vga_main_palette_ptr = (unsigned char *) exch_map_palette;

    /* Both audio gates closed, so the sound ids the shipped clips carry play
       nothing, and eight empty voices for the fdps_audio_stop_sample(-1) the
       teardown makes. */
    data_fdps_audio_sfx_enabled_flag = 0;
    data_fdps_audio_sfx_driver_available_flag = 0;
    for (index = 0; index < SFX_SAMPLE_SLOT_COUNT; index++) {
        data_fdps_audio_sample_handle_table[index] = NULL;
    }

    data_fdps_timer_tick_counter = 0;
}

/* One whole exchange, with everything a case can read afterwards copied out
   before anything else can move it. */
static void exch_run(void)
{
    int index;

    exch_snapshot_taken = 0;
    exch_snapshot_backdrop_id = -1;
    memset(exch_fill_snapshot, 0, (size_t) EXCH_FILL_BYTES);
    memset(exch_ramp_snapshot, 0, (size_t) EXCH_WINDOW_BYTES);
    memset(exch_cube_snapshot, 0, (size_t) EXCH_WINDOW_BYTES);
    for (index = 0; index < EXCH_READING_SLOTS; index++) {
        exch_snapshot_dac[index] = -1;
        exch_return_dac[index] = -1;
    }

    exch_set_mode(EXCH_MODE_320X200X256);
    memset((void *) EXCH_VGA_BASE, EXCH_SCREEN_SENTINEL,
           (size_t) EXCH_SCREEN_BYTES);
    for (index = 0; index < EXCH_DAC_ENTRIES; index++) {
        outp(EXCH_DAC_WRITE_INDEX, index);
        outp(EXCH_DAC_DATA, EXCH_DAC_SENTINEL);
        outp(EXCH_DAC_DATA, EXCH_DAC_SENTINEL);
        outp(EXCH_DAC_DATA, EXCH_DAC_SENTINEL);
    }

    exch_saved_timer = _dos_getvect(EXCH_TIMER_VECTOR);
    _dos_setvect(EXCH_TIMER_VECTOR, exch_timer_isr);
    fdps_combat_play_attack_exchange(ATTACKER, DEFENDER);
    _dos_setvect(EXCH_TIMER_VECTOR, exch_saved_timer);

    exch_read_dac(exch_return_dac);
    exch_screen_nonzero = 0;
    for (index = 0; index < EXCH_SCREEN_BYTES; index++) {
        if (((unsigned char *) EXCH_VGA_BASE)[index] != 0) {
            exch_screen_nonzero++;
        }
    }
    exch_set_mode(EXCH_MODE_TEXT);

    memmove(exch_ramp_at_return,
            (unsigned char *) data_fdps_palette_shade_ramp_table
            + EXCH_RAMP_WINDOW_AT, (size_t) EXCH_WINDOW_BYTES);
    memmove(exch_cube_at_return,
            data_fdps_inverse_palette_cube + EXCH_CUBE_WINDOW_AT,
            (size_t) EXCH_WINDOW_BYTES);

    exch_attacker_hp = (int) unit(ATTACKER)->hp_current;
    exch_defender_hp = (int) unit(DEFENDER)->hp_current;
    exch_xp_credit = data_fdps_battle_pending_xp_credit;
    exch_defender_paralysis =
        (int) unit(DEFENDER)->status_timers[PARALYSIS_SLOT];
}

/* The standard duel, played once and shared by the five cases that read it. */
static void exch_baseline(void)
{
    if (exch_baseline_done != 0) {
        return;
    }
    exch_stage(EXCH_STANDING_PORTRAIT);
    exch_run();
    exch_baseline_done = 1;
}

static void exch_read_file_window(char *name, long at, unsigned char *into)
{
    FILE *fp;

    memset(into, 0, (size_t) EXCH_WINDOW_BYTES);
    fp = fopen(name, "rb");
    if (fp != NULL) {
        fseek(fp, at, SEEK_SET);
        fread(into, 1, (size_t) EXCH_WINDOW_BYTES, fp);
        fclose(fp);
    }
}

/* The four strips as this file's own reading of the assembly composes them,
   over a surface pre-filled with `fill` so that two runs with different fills
   say which bytes the decode actually wrote. */
static void exch_build_fill_sheet(int first_sprite, unsigned char fill,
                                  unsigned char *into)
{
    unsigned char *sheet;
    int strip;

    memset(into, fill, (size_t) EXCH_FILL_BYTES);
    sheet = (unsigned char *) fdps_vfs_load_entry(exch_misc_archive,
                                                 exch_gauge_member);
    for (strip = 0; strip < EXCH_FILL_STRIPS; strip++) {
        fdps_cel_blit_sprite(sheet, strip + first_sprite, into,
                             EXCH_FILL_PITCH, 0,
                             strip * EXCH_FILL_STRIP_HEIGHT, 0, 0);
    }
    free(sheet);
}

/* How many of the bytes the decode really wrote -- the ones both fills agree
   on -- the snapshot disagrees with. */
static int exch_fill_mismatches(void)
{
    int offset;
    int wrong;

    wrong = 0;
    for (offset = 0; offset < EXCH_FILL_BYTES; offset++) {
        if (exch_fill_over_zero[offset] == exch_fill_over_ones[offset]
            && exch_fill_snapshot[offset] != exch_fill_over_zero[offset]) {
            wrong++;
        }
    }
    return wrong;
}

static int exch_fill_written_bytes(void)
{
    int offset;
    int written;

    written = 0;
    for (offset = 0; offset < EXCH_FILL_BYTES; offset++) {
        if (exch_fill_over_zero[offset] == exch_fill_over_ones[offset]) {
            written++;
        }
    }
    return written;
}

/* The offsets this body reads out of a record, and the two struct sizes the
   loads assume.  If any of them moved, every case below would be reading
   other bytes. */
static void the_record_offsets_the_exchange_reads(void)
{
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_x), 0);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_y), 1);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, portrait_id), 7);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, status_timers) +
             PARALYSIS_SLOT, 0x26);
    CHECK_EQ((int) sizeof(struct fdps_tile_attr_entry), 4);
    CHECK_EQ((int) offsetof(struct fdps_tile_attr_entry, combat_backdrop_id),
             EXCH_ATTR_BACKDROP_AT);
}

/* FMer1.tmp and FMer2.tmp are in the two globals for the length of the
   animation, and Mer1.tmp and Mer2.tmp are back in them when it returns.  The
   window compared is one the fight file and the map file disagree over, so
   neither half can pass by holding the other's bytes. */
static void the_fight_blend_tables_replace_the_map_ones_and_go_back(void)
{
    exch_baseline();
    CHECK_EQ(exch_snapshot_taken, 1);

    exch_read_file_window("FMER1.TMP", EXCH_RAMP_WINDOW_AT, exch_file_window);
    CHECK_EQ(memcmp(exch_ramp_snapshot, exch_file_window,
                    EXCH_WINDOW_BYTES), 0);
    exch_read_file_window("FMER2.TMP", EXCH_CUBE_WINDOW_AT, exch_file_window);
    CHECK_EQ(memcmp(exch_cube_snapshot, exch_file_window,
                    EXCH_WINDOW_BYTES), 0);

    exch_read_file_window("MER1.TMP", EXCH_RAMP_WINDOW_AT, exch_file_window);
    CHECK_EQ(memcmp(exch_ramp_at_return, exch_file_window,
                    EXCH_WINDOW_BYTES), 0);
    CHECK_EQ(memcmp(exch_ramp_snapshot, exch_file_window,
                    EXCH_WINDOW_BYTES) != 0, 1);
    exch_read_file_window("MER2.TMP", EXCH_CUBE_WINDOW_AT, exch_file_window);
    CHECK_EQ(memcmp(exch_cube_at_return, exch_file_window,
                    EXCH_WINDOW_BYTES), 0);
    CHECK_EQ(memcmp(exch_cube_snapshot, exch_file_window,
                    EXCH_WINDOW_BYTES) != 0, 1);
}

/* The 0x9c4-byte fill sheet holds FigBar.cel's sprites 4, 5, 6 and 7 laid five
   rows apart at a pitch of 0x7d -- and not its sprites 0 through 3, which is
   the sheet a body that started the run at the first sprite would have built. */
static void the_gauge_fill_sheet_is_figbars_last_four_sprites(void)
{
    exch_baseline();

    exch_build_fill_sheet(EXCH_FILL_FIRST_SPRITE, 0x00, exch_fill_over_zero);
    exch_build_fill_sheet(EXCH_FILL_FIRST_SPRITE, 0xff, exch_fill_over_ones);
    CHECK_EQ(exch_fill_written_bytes() > 0, 1);
    CHECK_EQ(exch_fill_mismatches(), 0);

    exch_build_fill_sheet(EXCH_FILL_WRONG_SPRITE, 0x00, exch_fill_over_zero);
    exch_build_fill_sheet(EXCH_FILL_WRONG_SPRITE, 0xff, exch_fill_over_ones);
    CHECK_EQ(exch_fill_mismatches() > 0, 1);
}

/* The teardown clears all 64000 bytes of the mode 13h screen and puts the map
   palette back over the whole DAC.  The reading taken during the run is the
   fight palette, at the same two entries, so the two uploads are told apart by
   which block each one came out of. */
static void the_screen_is_cleared_and_the_map_palette_uploaded(void)
{
    exch_baseline();

    CHECK_EQ(exch_screen_nonzero, 0);

    CHECK_EQ(exch_snapshot_dac[0], (int) exch_fight_palette[0].red);
    CHECK_EQ(exch_snapshot_dac[1], (int) exch_fight_palette[0].green);
    CHECK_EQ(exch_snapshot_dac[2], (int) exch_fight_palette[0].blue);
    CHECK_EQ(exch_snapshot_dac[3],
             (int) exch_fight_palette[EXCH_DAC_ENTRIES - 1].red);
    CHECK_EQ(exch_snapshot_dac[5],
             (int) exch_fight_palette[EXCH_DAC_ENTRIES - 1].blue);

    CHECK_EQ(exch_return_dac[0], (int) exch_map_palette[0].red);
    CHECK_EQ(exch_return_dac[1], (int) exch_map_palette[0].green);
    CHECK_EQ(exch_return_dac[2], (int) exch_map_palette[0].blue);
    CHECK_EQ(exch_return_dac[3],
             (int) exch_map_palette[EXCH_DAC_ENTRIES - 1].red);
    CHECK_EQ(exch_return_dac[5],
             (int) exch_map_palette[EXCH_DAC_ENTRIES - 1].blue);
}

/* The accumulator is zeroed before anything else happens, and this duel -- a
   side-1 attacker -- pays nothing into it, so the sentinel the fixture left is
   gone and nothing has replaced it. */
static void the_pending_experience_credit_is_cleared_on_entry(void)
{
    exch_baseline();

    CHECK_EQ(exch_xp_credit, 0);
}

/* An attack whose clip does not travel resolves the DEFENDER's tile and no
   other, and the id the Back%02d.saf name was composed from is that tile's,
   decremented.

   THE READING IS THE SNAPSHOT'S AND NOT THE ONE TAKEN AFTER THE RETURN.
   data_fdps_map_tile_combat_backdrop_id is a shared scratch slot, and
   fdps_combat_compute_hit_outcome republishes it for both combatants from
   inside every blow, so what is in it when the exchange returns is the last
   thing the BLOW looked up and says nothing about this function.  The snapshot
   is taken during fdps_combat_slide_in_attacker, which makes no terrain lookup
   of its own, so it still holds what the name was built from. */
static void a_standing_attack_opens_on_the_defenders_terrain(void)
{
    exch_baseline();

    CHECK_EQ(exch_snapshot_backdrop_id, EXCH_DEFENDER_BACKDROP_ID - 1);
}

/* Both blows are played: the attacker's takes the damage out of the defender
   and the counterblow takes it back out of the attacker. */
static void the_blow_and_the_counterblow_both_drain_a_record(void)
{
    exch_baseline();

    CHECK_EQ(exch_defender_hp < EXCH_START_HP, 1);
    CHECK_EQ(exch_attacker_hp < EXCH_START_HP, 1);
}

/* A clip that travels resolves the attacker's tile as well, second, so the id
   the global keeps is the ATTACKER's -- which is what makes the animation open
   on his terrain and slide across to the defender's. */
static void a_travelling_attack_opens_on_the_attackers_terrain(void)
{
    exch_stage(EXCH_TRAVELLING_PORTRAIT);
    exch_run();

    CHECK_EQ(exch_snapshot_backdrop_id, EXCH_ATTACKER_BACKDROP_ID - 1);
}

/* The guard is an unsigned test against zero, so a tile carrying no backdrop
   id is left alone rather than wrapping to 255. */
static void a_backdrop_id_of_zero_is_left_alone(void)
{
    exch_stage(EXCH_STANDING_PORTRAIT);
    exch_set_backdrop_ids(EXCH_ATTACKER_BACKDROP_ID, 0);
    exch_run();

    CHECK_EQ(exch_snapshot_backdrop_id, 0);
}

/* The counterattack test refuses a defender with nothing equipped, so no
   second blow is played and the attacker's record is never written. */
static void a_defender_with_no_weapon_never_strikes_back(void)
{
    exch_stage(EXCH_STANDING_PORTRAIT);
    unit(DEFENDER)->inventory_slots[0] = 0;
    unit(DEFENDER)->inventory_slots[1] = 0;
    exch_run();

    CHECK_EQ(exch_defender_hp < EXCH_START_HP, 1);
    CHECK_EQ(exch_attacker_hp, EXCH_START_HP);
}

/* THE TEST IS ASKED TWICE.  The first ask passes -- the defender is adjacent
   and armed, and his Act clip is loaded -- and then the blow itself paralyses
   him through fdps_combat_compute_hit_outcome, which is the first thing the
   second ask rejects on.  A body that kept the first answer would play the
   counterblow and the attacker's record would show it. */
static void an_ailment_the_blow_lands_cancels_the_counterblow(void)
{
    exch_stage(EXCH_STANDING_PORTRAIT);
    set_weapon(EFFECT_PARALYSIS, 100);
    exch_run();

    CHECK_EQ(exch_defender_paralysis > 0, 1);
    CHECK_EQ(exch_defender_hp < EXCH_START_HP, 1);
    CHECK_EQ(exch_attacker_hp, EXCH_START_HP);
}

/* A blow that empties the defender's HP makes fdps_combat_play_blow answer 0,
   and that answer alone stops the counterblow -- the counterattack test is
   never even reached. */
static void a_defender_the_blow_kills_never_strikes_back(void)
{
    exch_stage(EXCH_STANDING_PORTRAIT);
    unit(DEFENDER)->hp_current = EXCH_FATAL_HP;
    exch_run();

    CHECK_EQ(exch_defender_hp, 0);
    CHECK_EQ(exch_attacker_hp, EXCH_START_HP);
}

void run_combat_tests(void)
{
    RUN_TEST(the_record_layouts_match_the_offsets_read);
    RUN_TEST(every_one_of_the_six_slots_is_written);
    RUN_TEST(a_landed_blow_clears_the_miss_flag_and_reports_its_damage);
    RUN_TEST(a_missed_blow_reports_missed_and_no_damage);
    RUN_TEST(the_defenders_hp_record_is_never_written);
    RUN_TEST(evasion_is_subtracted_from_the_accuracy);
    RUN_TEST(damage_is_nine_tenths_of_the_stat_gap);
    RUN_TEST(a_negative_damage_is_clamped_to_zero);
    RUN_TEST(the_stat_words_are_read_unsigned);
    RUN_TEST(the_hp_word_is_read_unsigned_too);
    RUN_TEST(a_critical_halves_the_defense_and_is_reported);
    RUN_TEST(a_missed_blow_is_never_critical);
    RUN_TEST(the_critical_rate_comes_from_the_next_class_row);
    RUN_TEST(a_critical_weapon_adds_its_rate_to_the_class_rate);
    RUN_TEST(the_weapon_is_the_attackers_own);
    RUN_TEST(an_unarmed_attacker_reads_the_byte_before_the_inventory);
    RUN_TEST(poison_lasts_two_to_five_turns);
    RUN_TEST(paralysis_lasts_two_or_three_turns);
    RUN_TEST(a_zero_rate_never_lands_a_status);
    RUN_TEST(a_status_lands_even_when_the_blow_misses);
    RUN_TEST(an_immune_defender_takes_no_status_by_either_route);
    RUN_TEST(the_double_strike_effect_is_not_handled_here);
    RUN_TEST(terrain_scales_the_attackers_attack);
    RUN_TEST(terrain_scales_the_defenders_defense);
    RUN_TEST(a_flying_combatant_is_not_scaled_by_terrain);
    RUN_TEST(a_kill_pays_the_whole_award);
    RUN_TEST(a_survivor_pays_the_award_in_proportion);
    RUN_TEST(the_enemy_record_is_the_portrait_id_less_0x3c);
    RUN_TEST(a_portrait_id_above_ten_divides_by_thirty_more);
    RUN_TEST(the_award_is_gated_on_the_two_side_bytes);
    RUN_TEST(each_index_selects_its_own_record);

    RUN_TEST(only_the_last_of_the_eight_steps_is_left_on_the_page);
    RUN_TEST(the_y_is_the_pages_border_margin);
    RUN_TEST(the_incoming_backdrop_comes_to_rest_on_the_corner);
    RUN_TEST(the_outgoing_backdrop_ends_one_screen_away);
    RUN_TEST(the_arriving_sprite_rides_at_the_incoming_backdrops_x);
    RUN_TEST(the_cursor_is_advanced_once_per_step_after_the_draw);
    RUN_TEST(the_page_and_the_blend_slots_come_back_untouched);
    RUN_TEST(the_present_copies_the_window_at_the_border_corner);

    RUN_TEST(the_direction_is_taken_from_record_offset_six);
    RUN_TEST(nine_frames_advance_both_cursors_nine_times);
    RUN_TEST(attacker_only_leaves_the_defender_out_entirely);
    RUN_TEST(the_last_frame_has_everything_on_the_border_corner);
    RUN_TEST(each_combatant_gets_the_panel_his_side_byte_picks);
    RUN_TEST(a_side_two_attacker_closes_in_from_the_right);
    RUN_TEST(a_side_zero_attacker_closes_in_from_the_left);
    RUN_TEST(attacker_only_lets_the_attacker_ride_the_backdrop_down);

    RUN_TEST(the_record_offsets_the_exchange_reads);
    RUN_TEST(the_fight_blend_tables_replace_the_map_ones_and_go_back);
    RUN_TEST(the_gauge_fill_sheet_is_figbars_last_four_sprites);
    RUN_TEST(the_screen_is_cleared_and_the_map_palette_uploaded);
    RUN_TEST(the_pending_experience_credit_is_cleared_on_entry);
    RUN_TEST(a_standing_attack_opens_on_the_defenders_terrain);
    RUN_TEST(the_blow_and_the_counterblow_both_drain_a_record);
    RUN_TEST(a_travelling_attack_opens_on_the_attackers_terrain);
    RUN_TEST(a_backdrop_id_of_zero_is_left_alone);
    RUN_TEST(a_defender_with_no_weapon_never_strikes_back);
    RUN_TEST(an_ailment_the_blow_lands_cancels_the_counterblow);
    RUN_TEST(a_defender_the_blow_kills_never_strikes_back);
}
