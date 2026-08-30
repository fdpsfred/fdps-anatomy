/* tests/unitatk.c -- cover for src/unitatk.c.
 *
 * Every expected value below is read off the assembly of
 * fdps_unit_resolve_attack_hit at 0001c520 -- the six MOVSX word loads at
 * 0001c55f..0001c598 for which stat comes from which record, MOV AL,byte ptr
 * [EAX+0x20] / INC EAX for the class row, ADD EAX,0x8 for the critical byte,
 * MOV AL,byte ptr [EDX+0x9] and [EDX+0xa] for the weapon's effect and its
 * rate, the CMP against 0x3, 0x4 and 0x1 that pick the effect, LEA
 * EBX,[EDX+0x2] after IDIV by 4 and by 2 for the two status durations, CMP
 * EAX,0x19 for the immune class, the SAR pair that halves the defense, LEA
 * EDX,[EDX+EDX*8] with IDIV 10 for the damage, IDIV 9 for the random bonus,
 * the two JGE clamps at 0001c9ab and 0001c9e9, CMP EAX,0x2 on the side byte,
 * CMP EAX,0x3c on the portrait id, ADD byte ptr [EBP-0x8],0x1e for the guest
 * penalty and the two IDIVs that form the experience -- and from the record
 * layouts ticket 17 settled.  None of them is read off the emitted C.
 *
 * HOW THE RANDOMNESS IS TAKEN OUT.  Four rolls run through this function and
 * every one of them is rand() % 100 or rand() % n against a value the fixture
 * chooses, so each is driven to a certain answer rather than mocked:
 *
 *   - the accuracy roll lands on every draw when accuracy - evasion is 100 or
 *     more, and on none when it is 0 or less, because rand() % 100 is 0..99;
 *   - the critical roll is the same, against the critical rate;
 *   - the status roll is the same, against the weapon's percentage;
 *   - the random damage bonus is rand() % (damage / 9), which is skipped
 *     entirely while the damage is below 9 and is identically 0 when that
 *     ninth is exactly 1.  Every damage figure asserted below is one of those
 *     two cases.
 *
 * The two status durations are the one thing that cannot be pinned to a single
 * value -- they are rand() % 4 + 2 and rand() % 2 + 2 -- so those are asserted
 * as the bounds the assembly gives, over several blows.
 *
 * WHAT IS STAGED.  Every input this function has is a global or a record
 * reached through one: the unit array, the class, item and enemy tables, the
 * four scene layers fdps_map_load_tile_info reads, the two terrain modifier
 * tables and the master palette.  All of them are pointed at blocks built
 * here.  Nothing below asserts what any of those globals holds on its own --
 * ticket 23 owns that -- and the two modifier tables are zeroed by the fixture
 * so that a test which does not name a terrain effect cannot inherit one.
 *
 * WHY THE PALETTE IS THE LIVE DAC.  A status or critical flash uploads the
 * master palette four times, and its last upload is at bias 0, which puts back
 * exactly the bytes the pointer names.  Capturing the console's own DAC into
 * the staged palette therefore makes every flash end on the palette the report
 * is printed with, so no save-and-restore is needed and a failure line is
 * readable.
 */
#include <stddef.h>
#include <conio.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "unitatk.h"

/* The strides the four accessors in src/table.c and src/unit.c multiply by. */
#define UNIT_RECORD_STRIDE 0x50
#define ITEM_RECORD_STRIDE 0x17
#define CLASS_RECORD_STRIDE 0x0a
#define ENEMY_RECORD_STRIDE 0x0a

/* The two combatants' places in the staged unit array.  Four records are
   staged so that a walk which strayed into a neighbour would be visible. */
#define STAGE_UNITS 4
#define ATTACKER 0
#define TARGET 1

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
   (0, 0), which is cell 0 and so tile id 0, and the target on (1, 0), which is
   cell 1 and tile id 1 -- two different rows, so each side's terrain class is
   its own and a body that used one tile for both would be visible. */
#define ATTACKER_ATTR_ROW 0
#define TARGET_ATTR_ROW 1

#define DAC_ENTRY_COUNT 256
#define VGA_DAC_READ_INDEX 0x3c7
#define VGA_DAC_DATA 0x3c9

/* The weapon hit effect codes, from the CMP chain at 0001c6cd, 0001c6de and
   0001c7ca.  Effect 2, the double strike, is answered by the caller and has no
   arm here. */
#define EFFECT_NONE 0
#define EFFECT_PARALYSIS 1
#define EFFECT_DOUBLE_STRIKE 2
#define EFFECT_CRITICAL 3
#define EFFECT_POISON 4

/* CMP EAX,0x19 at 0001c70b and 0001c7f7. */
#define CLASS_IMMUNE 0x19

/* One of the five class codes fdps_unit_is_flying answers 1 for, used here to
   take a combatant out of the terrain path. */
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

/* CMP EAX,0x2 on the side byte and CMP EAX,0x3c on the portrait id. */
#define PLAYER_SIDE 2
#define FIRST_ENEMY_PORTRAIT 0x3c

/* A value no experience figure the fixture can produce could be, so "the
   global was not written" is distinguishable from "it was written with 0". */
#define XP_SENTINEL (-1)

/* Likewise for the hit-or-miss flag, whose two legal values are 0 and 1. */
#define FLAG_SENTINEL 0x55

static unsigned char unit_block[STAGE_UNITS * UNIT_RECORD_STRIDE];
static unsigned char item_block[STAGE_ITEMS * ITEM_RECORD_STRIDE];
static unsigned char class_block[STAGE_CLASSES * CLASS_RECORD_STRIDE];
static unsigned char enemy_block[STAGE_ENEMIES * ENEMY_RECORD_STRIDE];

static unsigned char stage_tile_map[TERRAIN_CELLS_AT + MAP_CELLS * 2];
static unsigned char stage_attr[ATTR_ROWS_AT + ATTR_ROWS * 4];
static unsigned char stage_grid[4 + MAP_CELLS * 2];
static unsigned char stage_event[EVENT_CELLS_AT + MAP_CELLS];

static unsigned char stage_pal[DAC_ENTRY_COUNT * 3];

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

/* Reads the console's own DAC into the staged master palette, so that the
   bias-0 upload that closes every flash restores the screen it started on. */
static void capture_live_palette(void)
{
    int entry;
    int component;

    for (entry = 0; entry < DAC_ENTRY_COUNT; entry++) {
        outp(VGA_DAC_READ_INDEX, entry);
        for (component = 0; component < 3; component++) {
            stage_pal[entry * 3 + component] =
                (unsigned char) (inp(VGA_DAC_DATA) & 0x3f);
        }
    }
}

/* Builds the standard duel and publishes every global the function reads.
 *
 * The attacker is a player-roster unit on side 1 with a plain equipped weapon;
 * the target is a level 10 unit with 20 of 40 HP.  Accuracy 200 against
 * evasion 0 is a difference of 200, so the blow lands on every draw, and AP
 * 100 against DP 91 is a gap of 9, which gives a damage of exactly 8 with no
 * random bonus.  So the baseline blow always leaves the target on 12.
 *
 * Both terrain modifier tables are cleared, so terrain is neutral until a test
 * asks for it; both combatants' class is 0, which is not one of the five
 * flying codes, so the terrain path does run and the tile lookups happen. */
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
    data_fdps_vga_main_palette_ptr = stage_pal;

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

    unit(TARGET)->pos_x = 1;
    unit(TARGET)->pos_y = 0;
    unit(TARGET)->side = 0;
    unit(TARGET)->portrait_id = 5;
    unit(TARGET)->clazz = 0;
    unit(TARGET)->level = 10;
    unit(TARGET)->dp = 91;
    unit(TARGET)->ev = 0;
    unit(TARGET)->hp_current = 20;
    unit(TARGET)->hp_max = 40;

    item(FIXTURE_WEAPON_ID)->type = WEAPON_ITEM_TYPE;
    item(FIXTURE_WEAPON_ID)->hit_effect = EFFECT_NONE;
    item(FIXTURE_WEAPON_ID)->hit_effect_rate = 0;

    /* The attacker's class is 0, so the critical rate comes from class row 1. */
    class_row(1)->critical = 0;

    enemy_row(0)->exp_reward = 60;
    enemy_row(1)->exp_reward = 30;

    data_fdps_battle_pending_xp_credit = XP_SENTINEL;
    data_fdps_battle_last_hit_or_miss_flag = FLAG_SENTINEL;
}

static void set_accuracy(int accuracy, int evasion)
{
    unit(ATTACKER)->hit = (short) accuracy;
    unit(TARGET)->ev = (short) evasion;
}

static void set_power(int attack, int defense)
{
    unit(ATTACKER)->ap = (short) attack;
    unit(TARGET)->dp = (short) defense;
}

static void set_hp(int current, int maximum)
{
    unit(TARGET)->hp_current = (short) current;
    unit(TARGET)->hp_max = (short) maximum;
}

static void set_weapon(int effect, int rate)
{
    item(FIXTURE_WEAPON_ID)->hit_effect = (unsigned char) effect;
    item(FIXTURE_WEAPON_ID)->hit_effect_rate = (unsigned char) rate;
}

/* The terrain class each combatant's own tile reports, written into byte +2 of
   that tile's attribute row. */
static void set_terrain(int attacker_terrain, int target_terrain)
{
    stage_attr[ATTR_ROWS_AT + ATTACKER_ATTR_ROW * 4 + 2] =
        (unsigned char) attacker_terrain;
    stage_attr[ATTR_ROWS_AT + TARGET_ATTR_ROW * 4 + 2] =
        (unsigned char) target_terrain;
}

/* Turns the attacker into a player-side unit striking an enemy record, which
   is the only combination that pays experience. */
static void make_it_an_enemy_kill_setup(void)
{
    unit(ATTACKER)->side = PLAYER_SIDE;
    unit(TARGET)->portrait_id = FIRST_ENEMY_PORTRAIT;
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

/* The baseline blow: it lands, it takes 8 off, and what it returns is what it
   wrote into the record. */
static void a_landed_blow_returns_the_hp_it_wrote_back(void)
{
    stage();
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 12);
    CHECK_EQ(unit(TARGET)->hp_current, 12);
}

/* A blow that misses leaves the HP where it was, still writes it back, and
   leaves the flag raised.  MOV byte ptr [0x00064010],0x1 on entry and the JGE
   at 0001c8cc that carries a miss straight to the write-back. */
static void a_missed_blow_leaves_the_hp_alone(void)
{
    stage();
    set_accuracy(0, 0);
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 20);
    CHECK_EQ(unit(TARGET)->hp_current, 20);
    CHECK_EQ(data_fdps_battle_last_hit_or_miss_flag, 1);

    stage();
    set_accuracy(50, 50);
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 20);

    stage();
    set_accuracy(0, 100);
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 20);
    CHECK_EQ(data_fdps_battle_last_hit_or_miss_flag, 1);
}

/* MOV byte ptr [0x00064010],0x0 at 0001c8d2, the first thing the hit branch
   does. */
static void the_flag_is_cleared_by_a_landed_blow(void)
{
    stage();
    fdps_unit_resolve_attack_hit(ATTACKER, TARGET);
    CHECK_EQ(data_fdps_battle_last_hit_or_miss_flag, 0);
}

/* The roll is against accuracy MINUS evasion and not against accuracy alone:
   100 against 0 lands on every draw, and the same 100 against 100 lands on
   none. */
static void evasion_is_subtracted_from_the_accuracy(void)
{
    stage();
    set_accuracy(100, 0);
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 12);

    stage();
    set_accuracy(100, 100);
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 20);

    stage();
    set_accuracy(250, 150);
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 12);
}

/* Damage is the stat gap times nine over ten, truncated: a gap of 9 gives 8, a
   gap of 2 gives 1 and a gap of 1 gives nothing at all.  A gap of 10 gives 9,
   whose ninth is exactly 1, and rand() % 1 is 0 -- so the random bonus is
   reached and contributes nothing, which is what tells that arm apart from one
   that was skipped. */
static void damage_is_nine_tenths_of_the_stat_gap(void)
{
    stage();
    set_power(100, 91);
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 12);

    stage();
    set_power(100, 98);
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 19);

    stage();
    set_power(100, 99);
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 20);

    stage();
    set_power(100, 90);
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 11);
}

/* A defender stronger than the attacker takes nothing.  The clamp is a
   separate CMP/JGE against 0 and not a max(): without it the negative damage
   would be subtracted from the HP and the target would gain 13 points from
   being hit. */
static void a_negative_damage_is_clamped_to_zero(void)
{
    stage();
    set_power(5, 20);
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 20);
    CHECK_EQ(unit(TARGET)->hp_current, 20);

    stage();
    set_power(0, 1000);
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 20);
}

/* The HP clamp is the second CMP/JGE against 0: a blow bigger than the HP
   leaves 0 and not a negative figure, and one exactly the size of the HP
   leaves 0 as well. */
static void the_hp_is_clamped_to_zero(void)
{
    stage();
    set_hp(5, 40);
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 0);
    CHECK_EQ(unit(TARGET)->hp_current, 0);

    stage();
    set_hp(8, 40);
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 0);
}

/* A critical halves the target's defense and nothing else: with AP 14 against
   DP 10 the gap is 4 and the damage 3, while the halved DP of 5 makes the gap
   9 and the damage 8.  The rate comes from byte +8 of class row clazz + 1. */
static void a_critical_halves_the_defense(void)
{
    stage();
    set_power(14, 10);
    class_row(1)->critical = 0;
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 17);

    stage();
    set_power(14, 10);
    class_row(1)->critical = 100;
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 12);
}

/* The class row is clazz + 1 and not clazz: an attacker of class 3 takes its
   critical rate from row 4, and row 3 is the neighbour that must not be
   read. */
static void the_critical_rate_comes_from_the_next_class_row(void)
{
    stage();
    set_power(14, 10);
    unit(ATTACKER)->clazz = 3;
    class_row(3)->critical = 0;
    class_row(4)->critical = 100;
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 12);

    stage();
    set_power(14, 10);
    unit(ATTACKER)->clazz = 3;
    class_row(3)->critical = 100;
    class_row(4)->critical = 0;
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 17);
}

/* Hit effect 3 does not roll: its percentage is added to the class rate and
   the ordinary critical roll decides.  With a class rate of 0 the weapon's 100
   is what makes every blow critical, and its 0 leaves every blow ordinary. */
static void a_critical_weapon_adds_its_rate_to_the_class_rate(void)
{
    stage();
    set_power(14, 10);
    set_weapon(EFFECT_CRITICAL, 100);
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 12);

    stage();
    set_power(14, 10);
    set_weapon(EFFECT_CRITICAL, 0);
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 17);
}

/* The weapon is the ATTACKER's equipped one.  The same critical weapon in the
   target's hands changes nothing. */
static void the_weapon_is_the_attackers_own(void)
{
    stage();
    set_power(14, 10);
    item(2)->type = WEAPON_ITEM_TYPE;
    item(2)->hit_effect = EFFECT_CRITICAL;
    item(2)->hit_effect_rate = 100;
    unit(TARGET)->inventory_slots[0] = ENTRY_EQUIPPED;
    unit(TARGET)->inventory_slots[1] = 2;
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 17);

    stage();
    set_power(14, 10);
    item(2)->type = WEAPON_ITEM_TYPE;
    item(2)->hit_effect = EFFECT_CRITICAL;
    item(2)->hit_effect_rate = 100;
    unit(ATTACKER)->inventory_slots[1] = 2;
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 12);
}

/* Nothing checks the slot.  An attacker with nothing equipped gets -1 back and
   the id lookup then reads the byte in front of the inventory field, which is
   record offset 0x09, and asks the item table for whatever id that holds. */
static void an_unarmed_attacker_reads_the_byte_before_the_inventory(void)
{
    stage();
    set_power(14, 10);
    unit(ATTACKER)->inventory_slots[0] = 0;
    unit(ATTACKER)->reserved_09 = 7;
    item(7)->hit_effect = EFFECT_CRITICAL;
    item(7)->hit_effect_rate = 100;
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 12);

    stage();
    set_power(14, 10);
    unit(ATTACKER)->inventory_slots[0] = 0;
    unit(ATTACKER)->reserved_09 = 8;
    item(8)->hit_effect = EFFECT_NONE;
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 17);
}

/* Effect 4 writes status_timers[3] and only that one, with a duration of
   rand() % 4 + 2, so it is 2, 3, 4 or 5 and never 0 or 1.  Eight blows are
   struck so that the bound is asserted against several draws rather than
   one. */
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
        fdps_unit_resolve_attack_hit(ATTACKER, TARGET);
        duration = (int) unit(TARGET)->status_timers[POISON_SLOT];
        if (duration < lowest) {
            lowest = duration;
        }
        if (duration > highest) {
            highest = duration;
        }
        CHECK_EQ(unit(TARGET)->status_timers[PARALYSIS_SLOT], 0);
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
        fdps_unit_resolve_attack_hit(ATTACKER, TARGET);
        duration = (int) unit(TARGET)->status_timers[PARALYSIS_SLOT];
        if (duration < lowest) {
            lowest = duration;
        }
        if (duration > highest) {
            highest = duration;
        }
        CHECK_EQ(unit(TARGET)->status_timers[POISON_SLOT], 0);
    }
    CHECK_EQ(lowest >= 2, 1);
    CHECK_EQ(highest <= 3, 1);
}

/* A rate of 0 never rolls under itself, so neither timer is written and no
   flash runs. */
static void a_zero_rate_never_lands_a_status(void)
{
    stage();
    set_weapon(EFFECT_POISON, 0);
    fdps_unit_resolve_attack_hit(ATTACKER, TARGET);
    CHECK_EQ(unit(TARGET)->status_timers[POISON_SLOT], 0);

    stage();
    set_weapon(EFFECT_PARALYSIS, 0);
    fdps_unit_resolve_attack_hit(ATTACKER, TARGET);
    CHECK_EQ(unit(TARGET)->status_timers[PARALYSIS_SLOT], 0);
}

/* The contract the whole function turns on: the status roll sits ABOVE the
   accuracy roll and is not inside it, so the ailment lands on a blow that then
   misses.  The HP is untouched and the flag is still raised, which is what
   says the blow really did miss. */
static void a_status_lands_even_when_the_blow_misses(void)
{
    stage();
    set_accuracy(0, 0);
    set_weapon(EFFECT_POISON, 100);
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 20);
    CHECK_EQ(unit(TARGET)->status_timers[POISON_SLOT] >= 2, 1);
    CHECK_EQ(data_fdps_battle_last_hit_or_miss_flag, 1);

    stage();
    set_accuracy(0, 0);
    set_weapon(EFFECT_PARALYSIS, 100);
    fdps_unit_resolve_attack_hit(ATTACKER, TARGET);
    CHECK_EQ(unit(TARGET)->status_timers[PARALYSIS_SLOT] >= 2, 1);
}

/* Class 0x19 takes neither ailment however certain the roll.  The test is on
   the class code alone, so the same unit still takes the damage. */
static void class_0x19_takes_no_status_effect(void)
{
    stage();
    unit(TARGET)->clazz = CLASS_IMMUNE;
    set_weapon(EFFECT_POISON, 100);
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 12);
    CHECK_EQ(unit(TARGET)->status_timers[POISON_SLOT], 0);

    stage();
    unit(TARGET)->clazz = CLASS_IMMUNE;
    set_weapon(EFFECT_PARALYSIS, 100);
    fdps_unit_resolve_attack_hit(ATTACKER, TARGET);
    CHECK_EQ(unit(TARGET)->status_timers[PARALYSIS_SLOT], 0);
}

/* Effect 2 has no arm here: it lands no ailment and adds nothing to the
   critical rate, so the blow resolves as an ordinary one.  With AP 14 against
   DP 10 an ordinary blow leaves 17 and a critical would leave 12. */
static void the_double_strike_effect_is_not_handled_here(void)
{
    stage();
    set_power(14, 10);
    set_weapon(EFFECT_DOUBLE_STRIKE, 100);
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 17);
    CHECK_EQ(unit(TARGET)->status_timers[POISON_SLOT], 0);
    CHECK_EQ(unit(TARGET)->status_timers[PARALYSIS_SLOT], 0);
}

/* The attack is scaled by the ATTACK table at the attacker's own terrain
   class: +50 percent turns an AP of 100 into 150, which against a DP of 141
   is a gap of 9 and a damage of 8.  Without the scaling the same blow cannot
   reach the defender at all.  The attacker's tile reports class 1 and the
   target's class 2, so an entry read from the wrong tile's class would be the
   zero left by the fixture. */
static void terrain_scales_the_attackers_attack(void)
{
    stage();
    set_terrain(1, 2);
    set_power(100, 141);
    data_fdps_battle_tile_attr_ap_modifier_table[1] = 50;
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 12);

    stage();
    set_terrain(1, 2);
    set_power(100, 141);
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 20);
}

/* The defense is scaled by the DEFENSE table at the target's own terrain
   class, and the entry's sign is behaviour: +50 percent turns a DP of 10 into
   15, which against an AP of 24 is a gap of 9 and a damage of 8, while the
   unscaled gap of 14 gives a damage of 12.  A NEGATIVE entry lowers the
   defense instead -- -50 percent turns a DP of 20 into 10, which against an AP
   of 19 becomes a gap of 9 where the unscaled blow could not connect. */
static void terrain_scales_the_targets_defense(void)
{
    stage();
    set_terrain(1, 2);
    set_power(24, 10);
    data_fdps_battle_tile_attr_def_modifier_table[2] = 50;
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 12);

    stage();
    set_terrain(1, 2);
    set_power(24, 10);
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 8);

    stage();
    set_terrain(1, 2);
    set_power(19, 20);
    data_fdps_battle_tile_attr_def_modifier_table[2] = -50;
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 12);

    stage();
    set_terrain(1, 2);
    set_power(19, 20);
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 20);
}

/* A flying combatant skips its own terrain lookup and keeps its raw stat,
   while the other side's scaling is unaffected. */
static void a_flying_combatant_is_not_scaled_by_terrain(void)
{
    stage();
    set_terrain(1, 2);
    set_power(100, 141);
    data_fdps_battle_tile_attr_ap_modifier_table[1] = 50;
    unit(ATTACKER)->clazz = CLASS_FLYING;
    class_row(CLASS_FLYING + 1)->critical = 0;
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 20);

    stage();
    set_terrain(1, 2);
    set_power(24, 10);
    data_fdps_battle_tile_attr_def_modifier_table[2] = 50;
    unit(TARGET)->clazz = CLASS_FLYING;
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 8);
}

/* The award is target level times the enemy record's reward over the
   attacker's level: 10 * 60 / 5 is 120, and a kill pays the whole of it. */
static void a_kill_pays_the_whole_award(void)
{
    stage();
    make_it_an_enemy_kill_setup();
    set_hp(8, 40);
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 0);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, 120);
}

/* A survivor pays the award scaled by the fraction of its maximum HP the blow
   took off: 120 * 8 / 40 is 24.  A blow that missed scales the same award by a
   damage of 0 and so pays nothing at all -- and it still writes the global,
   which is what tells 0 apart from "not written". */
static void a_survivor_pays_the_award_in_proportion(void)
{
    stage();
    make_it_an_enemy_kill_setup();
    set_hp(20, 40);
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 12);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, 24);

    stage();
    make_it_an_enemy_kill_setup();
    set_accuracy(0, 0);
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 20);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, 0);
}

/* The enemy record is the portrait id less 0x3c, so portrait 0x3d resolves
   record 1 and not record 0: 10 * 30 / 5 is 60. */
static void the_enemy_record_is_the_portrait_id_less_0x3c(void)
{
    stage();
    make_it_an_enemy_kill_setup();
    unit(TARGET)->portrait_id = FIRST_ENEMY_PORTRAIT + 1;
    set_hp(8, 40);
    CHECK_EQ(fdps_unit_resolve_attack_hit(ATTACKER, TARGET), 0);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, 60);
}

/* A portrait id above 10 is a guest, and 30 is added to the level the award is
   divided by: 10 * 60 / 35 is 17 where the same blow from portrait 10 pays
   120.  The test is > 10 and not >= 10, so portrait 10 is still permanent
   roster. */
static void a_guest_attacker_divides_by_thirty_more(void)
{
    stage();
    make_it_an_enemy_kill_setup();
    unit(ATTACKER)->portrait_id = 11;
    set_hp(8, 40);
    fdps_unit_resolve_attack_hit(ATTACKER, TARGET);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, 17);

    stage();
    make_it_an_enemy_kill_setup();
    unit(ATTACKER)->portrait_id = 10;
    set_hp(8, 40);
    fdps_unit_resolve_attack_hit(ATTACKER, TARGET);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, 120);
}

/* Both halves of the guard have to hold.  An attacker not on side 2, and a
   target whose portrait id is one below the enemy base, leave the global
   exactly as it was. */
static void no_award_unless_a_player_unit_struck_an_enemy(void)
{
    stage();
    unit(TARGET)->portrait_id = FIRST_ENEMY_PORTRAIT;
    set_hp(8, 40);
    fdps_unit_resolve_attack_hit(ATTACKER, TARGET);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SENTINEL);

    stage();
    unit(ATTACKER)->side = PLAYER_SIDE;
    unit(TARGET)->portrait_id = FIRST_ENEMY_PORTRAIT - 1;
    set_hp(8, 40);
    fdps_unit_resolve_attack_hit(ATTACKER, TARGET);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, XP_SENTINEL);

    stage();
    make_it_an_enemy_kill_setup();
    set_hp(8, 40);
    fdps_unit_resolve_attack_hit(ATTACKER, TARGET);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, 120);
}

/* Both indices select their own record.  The same duel run between units 2 and
   3 gives the same answer, and the records of units 0 and 1 are left alone. */
static void each_index_selects_its_own_record(void)
{
    stage();
    unit(2)->pos_x = 0;
    unit(2)->pos_y = 0;
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

    CHECK_EQ(fdps_unit_resolve_attack_hit(2, 3), 12);
    CHECK_EQ(unit(3)->hp_current, 12);
    CHECK_EQ(unit(TARGET)->hp_current, 20);
    CHECK_EQ(unit(ATTACKER)->hp_current, 0);
}

void run_unitatk_tests(void)
{
    capture_live_palette();

    RUN_TEST(the_record_layouts_match_the_offsets_read);
    RUN_TEST(a_landed_blow_returns_the_hp_it_wrote_back);
    RUN_TEST(a_missed_blow_leaves_the_hp_alone);
    RUN_TEST(the_flag_is_cleared_by_a_landed_blow);
    RUN_TEST(evasion_is_subtracted_from_the_accuracy);
    RUN_TEST(damage_is_nine_tenths_of_the_stat_gap);
    RUN_TEST(a_negative_damage_is_clamped_to_zero);
    RUN_TEST(the_hp_is_clamped_to_zero);
    RUN_TEST(a_critical_halves_the_defense);
    RUN_TEST(the_critical_rate_comes_from_the_next_class_row);
    RUN_TEST(a_critical_weapon_adds_its_rate_to_the_class_rate);
    RUN_TEST(the_weapon_is_the_attackers_own);
    RUN_TEST(an_unarmed_attacker_reads_the_byte_before_the_inventory);
    RUN_TEST(poison_lasts_two_to_five_turns);
    RUN_TEST(paralysis_lasts_two_or_three_turns);
    RUN_TEST(a_zero_rate_never_lands_a_status);
    RUN_TEST(a_status_lands_even_when_the_blow_misses);
    RUN_TEST(class_0x19_takes_no_status_effect);
    RUN_TEST(the_double_strike_effect_is_not_handled_here);
    RUN_TEST(terrain_scales_the_attackers_attack);
    RUN_TEST(terrain_scales_the_targets_defense);
    RUN_TEST(a_flying_combatant_is_not_scaled_by_terrain);
    RUN_TEST(a_kill_pays_the_whole_award);
    RUN_TEST(a_survivor_pays_the_award_in_proportion);
    RUN_TEST(the_enemy_record_is_the_portrait_id_less_0x3c);
    RUN_TEST(a_guest_attacker_divides_by_thirty_more);
    RUN_TEST(no_award_unless_a_player_unit_struck_an_enemy);
    RUN_TEST(each_index_selects_its_own_record);
}
