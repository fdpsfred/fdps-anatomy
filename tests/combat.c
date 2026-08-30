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
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
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
}
