/* item.c -- applying an item or spell effect to a list of battle-map targets,
 * and the in-battle item menu.
 *
 * See item.h for what a caller has to know.  Nothing here owns state: the unit
 * records are reached through the index the target list carries and the popup
 * queue belongs to indicat.c.
 */
#include <stdlib.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "aitarget.h"
#include "anim.h"
#include "audio.h"
#include "btlturn.h"
#include "death.h"
#include "indicat.h"
#include "keybd.h"
#include "mapcur.h"
#include "mapdraw.h"
#include "menu.h"
#include "movegrid.h"
#include "msgwin.h"
#include "table.h"
#include "text.h"
#include "unit.h"
#include "unititem.h"
#include "unitstat.h"
#include "vfs.h"
#include "item.h"

/* PUSH 0x0 at 00026280: the glyph id of digit zero in the damage digit set of
   the Number.cel sheet.  The healing path at 00026fd0 pushes 0x27 for its own
   set and the MP and stat-gain popups use 0x0d, so this base is what makes the
   number read as damage. */
#define DAMAGE_DIGIT_GLYPH_BASE 0

/* 00026230.  The loop is the plain -od shape: the guard at 00026243 compares
   the counter against the count with JL -- a SIGNED compare, so a negative
   target_count runs no iterations rather than walking the list as an enormous
   unsigned one -- and the increment block sits ahead of the body at 0002624d.

   ONE VALUE COMES BACK FROM A CALL AND IT IS USED.  fdps_unit_apply_damage's
   EAX is stored at 0002626f and pushed again at 00026285 as the popup's value,
   so what floats over the target is the damage that was ROLLED and not the HP
   the clamp let it take off.  fdps_show_number_indicator and
   fdps_play_indicator_queue return nothing the original looks at.

   THE TARGET ID IS READ FRESH FOR EACH OF THE TWO CALLS, at 0002625f and again
   at 00026278, and both times as a byte widened unsigned.  It is the same byte
   either way -- nothing between the two writes the list -- so keeping the two
   subscripts is a spelling of the assembly rather than a behaviour.

   THE QUEUE IS DRAINED ONCE, AFTER THE WHOLE LIST.  The single call at 00026290
   is outside the loop, so every target's number is played together over one
   shake and the damage has all landed before any of them is shown.  Draining
   inside the loop would animate the targets one after another. */
void fdps_apply_damage_to_targets(int target_count, unsigned char *target_ids,
                                  int base_damage)
{
    /* How far the walk over the target list has got. */
    int target_slot;
    /* What the hit on the current target rolled: the figure the popup shows,
       which is not the HP the target actually lost. */
    int damage_rolled;

    for (target_slot = 0; target_slot < target_count; target_slot++) {
        damage_rolled = fdps_unit_apply_damage(
            (int) target_ids[target_slot], base_damage);
        fdps_show_number_indicator(damage_rolled,
                                   (unsigned char) DAMAGE_DIGIT_GLYPH_BASE,
                                   (int) target_ids[target_slot]);
    }

    fdps_play_indicator_queue();
}

/* MOV EAX,0x61b98 / PUSH EAX at 00026fdc: the MISC.VFS member played over the
   whole target list before a single point of HP is restored.

   THE NAME IS MISSPELLED IN THE ORIGINAL AND HAS TO STAY MISSPELLED.  The
   literal is "Posion" and not "Poison", and this is the HEALING path, so it
   reads doubly wrong; the same spelling is emitted a second time at 0006155c
   for the map-AI callers, so it was written that way in more than one source
   module.  Correcting it does not merely lose the animation: a member
   fdps_vfs_load_entry cannot find prints and ends the process rather than
   coming back (anim.h), so the heal would take the game down.

   IT IS ALSO WRITTEN TO.  That loader upper-cases the CALLER'S storage in
   place, so this literal is folded to "POSION.SAF" by the first heal of the run
   and has to live in writable storage -- the same contract anim.c's
   ANIMATION_ARCHIVE and PLAYER_PHASE_ANIMATION carry
   (rebuild_info/pitfalls.md).  The lookup folds its query before comparing, so
   the second and every later heal still finds the member. */
#define HEAL_EFFECT_CLIP "Posion.saf"

/* PUSH 0xff at 00026ff2: the palette index every pixel of a flashed target
   becomes while the effect lands.  fdps_flash_units_in_color shifts the value
   left by eight itself before handing it on (indicat.h), so what is passed here
   is the colour and not the blit operand it becomes. */
#define HEAL_FLASH_COLOR 0xff

/* PUSH 0x27 at 0002704b: the glyph id of digit zero in the HEALING digit set of
   the Number.cel sheet.  fdps_apply_damage_to_targets above passes 0 for the
   damage digits and the MP and stat-gain popups pass 0x0d, so this base is the
   whole of what makes the number read as a heal rather than as a hit. */
#define HEAL_DIGIT_GLYPH_BASE 0x27

/* 00026fd0.  The same walk as fdps_apply_damage_to_targets with two whole
   animations bolted on the front: the guard at 0002700e compares the counter
   against the count with JL -- SIGNED, so a negative target_count runs no
   iterations rather than walking the list as an enormous unsigned one -- and
   the increment block sits ahead of the body at 00027018.

   BOTH ANIMATIONS RUN BEFORE THE LOOP GUARD IS EVER TESTED, and both are handed
   the caller's list and count unchanged.  The two CALLs at 00026fea and
   00026fff sit above the counter's initialisation at 00027007, so a
   target_count of 0 -- or a negative one -- still pays for the whole clip and
   the whole flash, about two and a half seconds of them, and heals nothing.
   Moving either call inside the loop or behind a count test is the obvious
   tidy-up and it changes what the player sees on every empty target list.

   ONE VALUE COMES BACK FROM A CALL AND IT IS USED.  fdps_unit_apply_heal's EAX
   is stored at 0002703a and pushed again at 00027050 as the popup's value, so
   what floats over the target is the heal that was ROLLED and not the HP the
   clamp let it take on.  fdps_play_vfs_animation_over_units,
   fdps_flash_units_in_color, fdps_show_number_indicator and
   fdps_play_indicator_queue return nothing the original looks at.

   THE TARGET ID IS READ FRESH FOR EACH OF THE TWO CALLS, at 00027027 and again
   at 00027040, and both times as a byte widened unsigned.  It is the same byte
   either way -- nothing between the two writes the list -- so keeping the two
   subscripts is a spelling of the assembly rather than a behaviour.

   THE QUEUE IS DRAINED ONCE, AFTER THE WHOLE LIST.  The single call at 0002705b
   is outside the loop, so every target's number is played together over one
   shake and every heal has landed before any of them is shown. */
void fdps_apply_heal_to_targets(int target_count, unsigned char *target_ids,
                                int base_heal)
{
    /* How far the walk over the target list has got. */
    int target_slot;
    /* What the heal on the current target rolled: the figure the popup shows,
       which is not the HP the target actually gained. */
    int heal_rolled;

    fdps_play_vfs_animation_over_units(target_count, target_ids,
                                       HEAL_EFFECT_CLIP);
    fdps_flash_units_in_color(target_count, target_ids, HEAL_FLASH_COLOR);

    for (target_slot = 0; target_slot < target_count; target_slot++) {
        heal_rolled = fdps_unit_apply_heal(
            (int) target_ids[target_slot], base_heal);
        fdps_show_number_indicator(heal_rolled,
                                   (unsigned char) HEAL_DIGIT_GLYPH_BASE,
                                   (int) target_ids[target_slot]);
    }

    fdps_play_indicator_queue();
}

/* ------------------------------------------------------------------------
 * fdps_apply_item_effect_to_targets @ 000262a0
 * ---------------------------------------------------------------------- */

/* The use-effect codes the dispatch below runs on, the byte at ITEM.DAT +0x0d.
   Which items carry which code is in assets/items.md; the pairing of an ITEM
   code with a WEAPON code that does the same thing is what the dispatch is
   built out of, because only the item half is consumed.

   0x05, 0x06, 0x0d and 0x1b are absent on purpose: they exist in ITEM.DAT --
   item 0xcc (no name), 光之水晶 and 空之寶石, 精靈之劍, 封咒手套 -- and
   match no branch here, so those items do nothing when used.  See item.h. */
#define USE_EFFECT_FIRE_ITEM 0x01
#define USE_EFFECT_THUNDER_ITEM 0x02
#define USE_EFFECT_ICE_ITEM 0x03
#define USE_EFFECT_EARTH_ITEM 0x04
#define USE_EFFECT_FIRE_WEAPON 0x07
#define USE_EFFECT_THUNDER_WEAPON 0x08
#define USE_EFFECT_ICE_WEAPON 0x09
#define USE_EFFECT_EARTH_WEAPON 0x0a
#define USE_EFFECT_RESTORE_HP_ITEM 0x0b
#define USE_EFFECT_RESTORE_MP_ITEM 0x0c
#define USE_EFFECT_MAX_HP_UP 0x0f
#define USE_EFFECT_MAX_MP_UP 0x10
#define USE_EFFECT_AP_UP 0x11
#define USE_EFFECT_DP_UP 0x12
#define USE_EFFECT_DX_UP 0x13
#define USE_EFFECT_MOVE_UP 0x14
#define USE_EFFECT_CURE_POISON 0x16
#define USE_EFFECT_CURE_PARALYSIS 0x18
#define USE_EFFECT_BEAM_CANNON 0x1e
#define USE_EFFECT_RESTORE_HP_WEAPON 0x20
#define USE_EFFECT_GAIA_COMBAT_KIT 0x21
#define USE_EFFECT_GAIA_HP_KIT 0x22
#define USE_EFFECT_BRANDO_DEVICE 0x23

/* The MISC.VFS members the four elemental effects and the two restore effects
   play over the target list: MOV EAX,0x61b50 / 0x61b5c / 0x61b68 / 0x61b80 /
   0x61b8c in front of the fdps_play_vfs_animation_over_units calls, and MOV
   EAX,0x61b74 with MOV EAX,0x60128 at 0002646d for the earthquake's sample.

   THE NAMES ARE WRITTEN TO.  fdps_vfs_load_entry upper-cases the CALLER'S
   storage in place before it looks a member up (anim.h), so every one of these
   has to live in writable storage -- the same contract anim.c's
   ANIMATION_ARCHIVE carries (rebuild_info/pitfalls.md).  A member the loader
   cannot find prints and ends the process rather than coming back, so a
   mistyped name here does not merely lose the animation. */
#define EFFECT_ARCHIVE "MISC.VFS"
#define FIRE_EFFECT_CLIP "EMg00.saf"
#define THUNDER_EFFECT_CLIP "EMg05.saf"
#define ICE_EFFECT_CLIP "EMg08.saf"
#define CURE_EFFECT_CLIP "Cure.saf"
#define RESTORE_MP_EFFECT_CLIP "CureMP.saf"
#define EARTHQUAKE_SAMPLE "EarQu.wav"

/* The earth effect's screen shake, PUSH 0x1 at 00026482 and the loop at
   000264a7 through 00026504: the sample is started once and the view is then
   re-rendered 25 times with both scroll origins displaced.

   THE JITTER IS NOT SYMMETRIC.  IDIV EBX with EBX = 4 and SUB EDX,0x2 at
   000264c8 and 000264e1 makes it rand() % 4 - 2, which is -2, -1, 0 or +1: the
   view leans one pixel up and left on average for the whole quake.  Writing it
   as a symmetric -2..+2 shake, which is what it looks like it ought to be,
   moves the picture.

   BOTH DRAWS HAPPEN EVERY FRAME, before either origin is written, so the quake
   costs the shared rand() stream exactly fifty values and every later roll in
   the battle is shifted by that many.  The same shape and the same three
   numbers are in spell.c's 裂地術. */
#define EARTHQUAKE_SAMPLE_PLAY_ONCE 1
#define EARTHQUAKE_SHAKE_FRAMES 0x19
#define EARTHQUAKE_SHAKE_SPREAD 4
#define EARTHQUAKE_SHAKE_BIAS 2

/* PUSH 0xd in front of every stat-gain and MP-restore popup (00026590,
   00026601, 00026670, 000266df, 00026761, 000267e3, 00026a34): the glyph id of
   digit zero in the GAIN digit set of the Number.cel sheet.  The damage digits
   are base 0 and the healing digits base 0x27; the direct-damage branch at
   0x1e passes 0 like the rest of the damage paths. */
#define GAIN_DIGIT_GLYPH_BASE 0x0d

/* PUSH 0xff at 000268be, 0002694a and 0002699d: the palette index every pixel
   of a flashed target becomes.  fdps_flash_units_in_color shifts the value left
   by eight itself (indicat.h), so what is passed here is the colour and not the
   blit operand it becomes. */
#define EFFECT_FLASH_COLOR 0xff

/* How much each permanent stat-up item moves its stat.  Every one of these is a
   literal in the instruction that applies it -- ADD word ptr [EAX+0x42],0xf at
   000265ef and its five siblings -- and NOT the item's use_amount, which is 0
   on all six records (assets/items.md).  Writing the obvious `stat +=
   use_amount` compiles, runs, and silently does nothing. */
#define MAX_HP_UP_AMOUNT 0x0f
#define MAX_MP_UP_AMOUNT 0x0f
#define AP_UP_AMOUNT 7
#define DP_UP_AMOUNT 7
#define DX_UP_AMOUNT 7
#define MOVE_UP_AMOUNT 1

/* 蓋亞's two 強化套件 and 布蘭多's 高能量裝置: who they work on, what they
   give, and the message ids they draw.

   THE TWO KITS TEST THE FORM ID AT +0x07 AND THE DEVICE TESTS THE CHARACTER ID
   AT +0x08.  MOV AL,byte ptr [EAX+0x7] at 00026b14 and 00026bfd against MOV
   AL,byte ptr [EAX+0x8] at 00026cb0.  The two fields agree for a unit that has
   never class-changed and part company for one that has, so unifying them is a
   behaviour change either way round: onto +0x07 breaks the 高能量砲 trade for a
   class-changed 布蘭多, onto +0x08 changes which portrait the message window
   shows the kits under. */
#define GAIA_FORM_ID 9
#define BRANDO_CHARACTER_ID 8
#define GAIA_AP_UP_AMOUNT 0x1e
#define GAIA_DP_UP_AMOUNT 0x1e
#define GAIA_MOVE_UP_AMOUNT 1
#define GAIA_MAX_HP_UP_AMOUNT 100
#define THUNDER_GOD_CANNON_SPELL 0x1d
#define METAL_ORE_ITEM_ID 0xa3
#define HIGH_ENERGY_CANNON_ITEM_ID 0xbe
#define NO_ITEM_SLOT (-1)

/* The four message entries the three upgrade branches draw out of the shared
   text block, PUSH 0x21d / 0x21e / 0x21f / 0x220 / 0x221.  0x21d is the one
   refusal all three share for a unit that is not the character the item was
   made for. */
#define UPGRADE_REFUSED_TEXT_ID 0x21d
#define GAIA_COMBAT_KIT_TEXT_ID 0x21e
#define GAIA_HP_KIT_TEXT_ID 0x21f
#define BRANDO_NO_ORE_TEXT_ID 0x220
#define BRANDO_TRADE_TEXT_ID 0x221

/* Where the upgrade messages are drawn and in what colours, PUSH 0xaa44a /
   PUSH 0x140 / PUSH 0xd0 / PUSH 0x0 / PUSH 0x6d in front of every one of the
   seven fdps_draw_text calls.  0xaa44a is screen (138, 131), the pen inside the
   message panel; it stays a literal because it is an address inside the display
   adapter's aperture rather than the address of anything the linker places
   (rebuild_info/pitfalls.md, contract E). */
#define PANEL_TEXT_ORIGIN 0x000aa44a
#define VGA_SCREEN_PITCH 0x140
#define MESSAGE_FG_COLOR 0xd0
#define MESSAGE_BG_COLOR 0
#define MESSAGE_OUTLINE_COLOR 0x6d

/* How big the buffer fdps_collect_death_scripts is handed is: 100 bytes,
   thirty-three three-byte records.  The array runs from [EBP-0xa0], the address
   pushed at 00026dc6, up to the item-id slot at [EBP-0x3c].  That collector is
   given no capacity and writes one record per dead unit on the map, so the
   figure is a contract with it and not a preference (death.h). */
#define DEATH_SCRIPT_BUFFER_BYTES 100

/* How the acting unit's bag slot becomes an item id: MOV EDX,item_slot / ADD
   EDX,EDX / ADD EDX,record / MOV AL,byte ptr [EDX+0xb] at 000262f4.  The bag is
   eight two-byte entries starting at record +0x0a and the id is the SECOND byte
   of the entry, so the subscript is 2 * slot + 1 and its largest value is 15,
   inside the sixteen inventory_slots holds. */
#define BAG_ENTRY_ID_INDEX(slot) ((slot) * 2 + 1)

/* 000262a0.  One long dispatch on the item's use-effect byte followed by one
   death settlement, and the shape of the assembly is the shape of the C: the
   fourteen codes from 0x01 to 0x0c form a single else-if chain that every arm
   leaves with a JMP to 00026a59, and 0x1e, 0x21, 0x22 and 0x23 are four
   SEPARATE ifs after it, each re-reading the same byte.  Nothing can match two
   of them, so the split costs no behaviour, but it is what the original wrote
   and the tail below is reached the same way from every one of them.

   THE ARMS THAT SHARE A BODY DO NOT SHARE A CONSUME.  Each elemental arm tests
   the code a SECOND time after the damage has landed -- CMP EAX,0x1 / JNZ at
   0002637b and its three siblings -- and only the item half calls
   fdps_unit_remove_item.  Collapsing the two tests into one is what loses the
   distinction between a 炎之珠 and a 聖火之劍.

   ELEVEN VALUES COME BACK FROM CALLS AND ARE USED.  fdps_get_unit_record's
   pointer at 000262ee, 0002657c and its seven repeats in the stat-up arms,
   00026882, 0002690e, 000269e4, 00026a96, 00026afa, 00026be3 and 00026c96;
   fdps_get_item_record's at 00026322; fdps_unit_restore_mp's roll at 00026a23;
   fdps_unit_apply_damage's roll at 00026ab3; fdps_unit_find_item_slot's slot at
   00026d00 and 00026d85; and fdps_collect_death_scripts' count at 00026dd5.
   Every other call's EAX is dropped -- fdps_audio_start_wav's voice handle at
   00026488, all seven fdps_draw_text cursors and fdps_unit_add_item's answer at
   00026daf among them -- so nothing here notices a failure any of them
   reports.

   ONE OF THOSE POINTERS IS FETCHED AND THROWN AWAY.  The record read at
   00026a96 inside the 0x1e loop is stored to [EBP-0x20] and never read: the
   direct-damage arm works entirely through unit indices.  The build is -od,
   which does not delete a dead store (rebuild_info/build_flags.md), so the call
   is what the source said rather than what an optimiser left, and it stays.

   THE DEATH TAIL IS UNCONDITIONAL and its three steps are in the only order
   that pays out: collect the scripts of everything now at or below zero HP,
   THEN play the destruction sequence -- which sets the very flag the collect
   rejects -- and only then run what was collected (death.h).  It runs even for
   the four effect codes that matched nothing, which costs one sweep of the unit
   array and, with nothing dead, nothing else. */
void fdps_apply_item_effect_to_targets(int unit_index, int item_slot,
                                       int target_count,
                                       unsigned char *target_ids)
{
    /* Where fdps_collect_death_scripts packs what this effect killed. */
    unsigned char death_scripts[DEATH_SCRIPT_BUFFER_BYTES];
    /* The unit doing the using, read once so its bag can be indexed. */
    struct fdps_unit_record *actor;
    /* The ITEM.DAT record of whatever sat in that bag slot. */
    struct fdps_item_effect *item;
    /* The id the bag slot held.  The original spills it twice on the way to
       the lookup, at [EBP-0xb8] and [EBP-0x3c]; frame allocation is not part of
       the standard (ADR-0001). */
    int item_id;
    /* The record's use-effect byte, widened without sign, which the whole
       dispatch runs on. */
    unsigned char use_effect;
    /* The record's use-amount, the SIGNED word at +0x0e widened by MOVSX at
       00026328.  It is the power of every damage, heal and MP restore here and
       is 0 on every one of the permanent stat-up items. */
    int use_amount;
    /* EarQu.wav, held just long enough to be handed to the mixer.  It is never
       freed: the buffer leaks once per earth item used, which is the
       original's behaviour. */
    void *earthquake_sample;
    /* Where the view sat before the quake, and where it is put back to. */
    int saved_origin_x;
    int saved_origin_y;
    /* This shake frame's displacement from that, -2 to +1 on each axis. */
    int shake_offset_x;
    int shake_offset_y;
    /* Which entry of target_ids the two walks are on. */
    int target_slot;
    /* Which of the quake's 25 re-renders is being drawn.  The original keeps
       this, target_slot above and metal_ore_slot below in the one frame slot at
       [EBP-0x2c] and carries nothing between them; frame allocation is not part
       of the standard (ADR-0001). */
    int shake_frame;
    /* The record the stat-up arms, the MP-restore walk and the direct-damage
       walk are working on.  One frame slot in the original, [EBP-0x20]. */
    struct fdps_unit_record *target;
    /* The record whose poison or paralysis byte is being cleared. */
    struct fdps_unit_record *cure_target;
    /* The record of the one named character an upgrade item acts on. */
    struct fdps_unit_record *upgrade_target;
    /* What the MP restore or the direct hit rolled: the figure the popup shows,
       which is not the points the clamp let it move. */
    int amount_rolled;
    /* Which bag slot of the 布蘭多 target holds the 金屬礦, looked up a SECOND
       time. */
    int metal_ore_slot;
    /* How many records fdps_collect_death_scripts wrote. */
    int death_script_count;

    data_fdps_indicator_queue_count = 0;
    actor = fdps_get_unit_record(unit_index);
    item_id = (int) actor->inventory_slots[BAG_ENTRY_ID_INDEX(item_slot)];
    item = fdps_get_item_record(item_id);
    use_amount = item->use_amount;
    use_effect = item->use_effect;

    if (use_effect == USE_EFFECT_FIRE_ITEM
        || use_effect == USE_EFFECT_FIRE_WEAPON) {
        fdps_play_vfs_animation_over_units(target_count, target_ids,
                                           FIRE_EFFECT_CLIP);
        fdps_apply_damage_to_targets(target_count, target_ids, use_amount);
        if (use_effect == USE_EFFECT_FIRE_ITEM) {
            fdps_unit_remove_item(unit_index, item_slot);
        }
    } else if (use_effect == USE_EFFECT_THUNDER_ITEM
               || use_effect == USE_EFFECT_THUNDER_WEAPON) {
        fdps_play_vfs_animation_over_units(target_count, target_ids,
                                           THUNDER_EFFECT_CLIP);
        fdps_apply_damage_to_targets(target_count, target_ids, use_amount);
        if (use_effect == USE_EFFECT_THUNDER_ITEM) {
            fdps_unit_remove_item(unit_index, item_slot);
        }
    } else if (use_effect == USE_EFFECT_ICE_ITEM
               || use_effect == USE_EFFECT_ICE_WEAPON) {
        fdps_play_vfs_animation_over_units(target_count, target_ids,
                                           ICE_EFFECT_CLIP);
        fdps_apply_damage_to_targets(target_count, target_ids, use_amount);
        if (use_effect == USE_EFFECT_ICE_ITEM) {
            fdps_unit_remove_item(unit_index, item_slot);
        }
    } else if (use_effect == USE_EFFECT_EARTH_ITEM
               || use_effect == USE_EFFECT_EARTH_WEAPON) {
        /* The earth effect has no clip of its own: the sample and the shake
           ARE the animation, and the damage lands after the view has been put
           back. */
        earthquake_sample = fdps_vfs_load_entry(EFFECT_ARCHIVE,
                                               EARTHQUAKE_SAMPLE);
        fdps_audio_start_wav(earthquake_sample, EARTHQUAKE_SAMPLE_PLAY_ONCE,
                             SFX_WAV_RATE_FROM_HEADER,
                             SFX_WAV_VOLUME_FROM_DEFAULT);

        saved_origin_x = data_fdps_battle_view_window_origin_x;
        saved_origin_y = data_fdps_battle_view_window_origin_y;

        for (shake_frame = 0; shake_frame < EARTHQUAKE_SHAKE_FRAMES;
             shake_frame++) {
            shake_offset_x = rand() % EARTHQUAKE_SHAKE_SPREAD
                             - EARTHQUAKE_SHAKE_BIAS;
            shake_offset_y = rand() % EARTHQUAKE_SHAKE_SPREAD
                             - EARTHQUAKE_SHAKE_BIAS;
            data_fdps_battle_view_window_origin_x =
                saved_origin_x + shake_offset_x;
            data_fdps_battle_view_window_origin_y =
                saved_origin_y + shake_offset_y;
            fdps_render_view_frame();
        }

        data_fdps_battle_view_window_origin_x = saved_origin_x;
        data_fdps_battle_view_window_origin_y = saved_origin_y;
        fdps_apply_damage_to_targets(target_count, target_ids, use_amount);
        if (use_effect == USE_EFFECT_EARTH_ITEM) {
            fdps_unit_remove_item(unit_index, item_slot);
        }
    } else if (use_effect == USE_EFFECT_MOVE_UP) {
        fdps_play_vfs_animation_over_units(target_count, target_ids,
                                           CURE_EFFECT_CLIP);
        target = fdps_get_unit_record((int) target_ids[0]);
        target->move = (unsigned char) (target->move + MOVE_UP_AMOUNT);
        fdps_show_number_indicator(MOVE_UP_AMOUNT,
                                   (unsigned char) GAIN_DIGIT_GLYPH_BASE,
                                   (int) target_ids[0]);
        fdps_unit_remove_item(unit_index, item_slot);
        fdps_play_indicator_queue();
    } else if (use_effect == USE_EFFECT_MAX_HP_UP) {
        fdps_play_vfs_animation_over_units(target_count, target_ids,
                                           CURE_EFFECT_CLIP);
        target = fdps_get_unit_record((int) target_ids[0]);
        target->hp_max = (short) (target->hp_max + MAX_HP_UP_AMOUNT);
        fdps_show_number_indicator(MAX_HP_UP_AMOUNT,
                                   (unsigned char) GAIN_DIGIT_GLYPH_BASE,
                                   (int) target_ids[0]);
        fdps_unit_remove_item(unit_index, item_slot);
        fdps_play_indicator_queue();
    } else if (use_effect == USE_EFFECT_MAX_MP_UP) {
        fdps_play_vfs_animation_over_units(target_count, target_ids,
                                           CURE_EFFECT_CLIP);
        target = fdps_get_unit_record((int) target_ids[0]);
        target->mp_max = (short) (target->mp_max + MAX_MP_UP_AMOUNT);
        fdps_show_number_indicator(MAX_MP_UP_AMOUNT,
                                   (unsigned char) GAIN_DIGIT_GLYPH_BASE,
                                   (int) target_ids[0]);
        fdps_unit_remove_item(unit_index, item_slot);
        fdps_play_indicator_queue();
    } else if (use_effect == USE_EFFECT_AP_UP) {
        fdps_play_vfs_animation_over_units(target_count, target_ids,
                                           CURE_EFFECT_CLIP);
        target = fdps_get_unit_record((int) target_ids[0]);
        target->ap_base = (short) (target->ap_base + AP_UP_AMOUNT);
        fdps_show_number_indicator(AP_UP_AMOUNT,
                                   (unsigned char) GAIN_DIGIT_GLYPH_BASE,
                                   (int) target_ids[0]);
        fdps_unit_remove_item(unit_index, item_slot);
        /* Only the three arms that move a COMBAT stat recompute: the two
           maximum-point arms and the movement arm above do not, because
           nothing derived hangs off those three fields. */
        fdps_unit_recompute_combat_stats((int) target_ids[0]);
        fdps_play_indicator_queue();
    } else if (use_effect == USE_EFFECT_DP_UP) {
        fdps_play_vfs_animation_over_units(target_count, target_ids,
                                           CURE_EFFECT_CLIP);
        target = fdps_get_unit_record((int) target_ids[0]);
        target->dp_base = (short) (target->dp_base + DP_UP_AMOUNT);
        fdps_show_number_indicator(DP_UP_AMOUNT,
                                   (unsigned char) GAIN_DIGIT_GLYPH_BASE,
                                   (int) target_ids[0]);
        fdps_unit_remove_item(unit_index, item_slot);
        fdps_unit_recompute_combat_stats((int) target_ids[0]);
        fdps_play_indicator_queue();
    } else if (use_effect == USE_EFFECT_DX_UP) {
        fdps_play_vfs_animation_over_units(target_count, target_ids,
                                           CURE_EFFECT_CLIP);
        target = fdps_get_unit_record((int) target_ids[0]);
        target->dx_base = (short) (target->dx_base + DX_UP_AMOUNT);
        fdps_show_number_indicator(DX_UP_AMOUNT,
                                   (unsigned char) GAIN_DIGIT_GLYPH_BASE,
                                   (int) target_ids[0]);
        fdps_unit_remove_item(unit_index, item_slot);
        fdps_unit_recompute_combat_stats((int) target_ids[0]);
        fdps_play_indicator_queue();
    } else if (use_effect == USE_EFFECT_RESTORE_HP_ITEM
               || use_effect == USE_EFFECT_RESTORE_HP_WEAPON) {
        /* The only arm with no animation of its own: the clip, the flash and
           the queue drain all live inside fdps_apply_heal_to_targets. */
        fdps_apply_heal_to_targets(target_count, target_ids, use_amount);
        if (use_effect == USE_EFFECT_RESTORE_HP_ITEM) {
            fdps_unit_remove_item(unit_index, item_slot);
        }
    } else if (use_effect == USE_EFFECT_CURE_POISON) {
        cure_target = fdps_get_unit_record((int) target_ids[0]);
        /* The popup is queued only when there was something to cure, but the
           byte is cleared either way, and both happen BEFORE the clip: the
           cure has already landed by the time the player sees it. */
        if (cure_target->status_timers[3] != 0) {
            fdps_show_cure_indicator((int) target_ids[0]);
        }
        cure_target->status_timers[3] = 0;
        fdps_play_vfs_animation_over_units(target_count, target_ids,
                                           CURE_EFFECT_CLIP);
        fdps_flash_units_in_color(target_count, target_ids,
                                  EFFECT_FLASH_COLOR);
        fdps_unit_remove_item(unit_index, item_slot);
        fdps_play_indicator_queue();
    } else if (use_effect == USE_EFFECT_CURE_PARALYSIS) {
        cure_target = fdps_get_unit_record((int) target_ids[0]);
        if (cure_target->status_timers[4] != 0) {
            fdps_show_cure_indicator((int) target_ids[0]);
        }
        cure_target->status_timers[4] = 0;
        fdps_play_vfs_animation_over_units(target_count, target_ids,
                                           CURE_EFFECT_CLIP);
        fdps_flash_units_in_color(target_count, target_ids,
                                  EFFECT_FLASH_COLOR);
        fdps_unit_remove_item(unit_index, item_slot);
        fdps_play_indicator_queue();
    } else if (use_effect == USE_EFFECT_RESTORE_MP_ITEM) {
        fdps_play_vfs_animation_over_units(target_count, target_ids,
                                           RESTORE_MP_EFFECT_CLIP);
        fdps_flash_units_in_color(target_count, target_ids,
                                  EFFECT_FLASH_COLOR);
        for (target_slot = 0; target_slot < target_count; target_slot++) {
            target = fdps_get_unit_record((int) target_ids[target_slot]);
            /* A unit with no MP at all gets MISS rather than a zero: the test
               is on the MAXIMUM at +0x46 and not on what it currently has, so
               a spent caster is still restored. */
            if (target->mp_max == 0) {
                fdps_show_miss_indicator((int) target_ids[target_slot]);
            } else {
                amount_rolled = fdps_unit_restore_mp(
                    (int) target_ids[target_slot], use_amount);
                fdps_show_number_indicator(
                    amount_rolled, (unsigned char) GAIN_DIGIT_GLYPH_BASE,
                    (int) target_ids[target_slot]);
            }
        }
        fdps_unit_remove_item(unit_index, item_slot);
        fdps_play_indicator_queue();
    }

    if (use_effect == USE_EFFECT_BEAM_CANNON) {
        for (target_slot = 0; target_slot < target_count; target_slot++) {
            /* Fetched and dropped -- see the note above the function. */
            target = fdps_get_unit_record((int) target_ids[target_slot]);
            amount_rolled = fdps_unit_apply_damage(
                (int) target_ids[target_slot], use_amount);
            fdps_show_number_indicator(
                amount_rolled, (unsigned char) DAMAGE_DIGIT_GLYPH_BASE,
                (int) target_ids[target_slot]);
        }
        fdps_play_indicator_queue();
    }

    if (use_effect == USE_EFFECT_GAIA_COMBAT_KIT) {
        upgrade_target = fdps_get_unit_record((int) target_ids[0]);
        fdps_message_window_open((int) upgrade_target->portrait_id);
        if (upgrade_target->portrait_id == GAIA_FORM_ID) {
            fdps_draw_text(data_fdps_all_game_text_ptr,
                           GAIA_COMBAT_KIT_TEXT_ID,
                           (unsigned char *) PANEL_TEXT_ORIGIN,
                           VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                           MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
            upgrade_target->ap_base =
                (short) (upgrade_target->ap_base + GAIA_AP_UP_AMOUNT);
            upgrade_target->dp_base =
                (short) (upgrade_target->dp_base + GAIA_DP_UP_AMOUNT);
            upgrade_target->move =
                (unsigned char) (upgrade_target->move + GAIA_MOVE_UP_AMOUNT);
            fdps_set_flag_bit((int) target_ids[0], THUNDER_GOD_CANNON_SPELL);
            fdps_unit_remove_item(unit_index, item_slot);
            fdps_unit_recompute_combat_stats((int) target_ids[0]);
        } else {
            fdps_draw_text(data_fdps_all_game_text_ptr,
                           UPGRADE_REFUSED_TEXT_ID,
                           (unsigned char *) PANEL_TEXT_ORIGIN,
                           VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                           MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
        }
        fdps_message_window_close();
    }

    if (use_effect == USE_EFFECT_GAIA_HP_KIT) {
        upgrade_target = fdps_get_unit_record((int) target_ids[0]);
        fdps_message_window_open((int) upgrade_target->portrait_id);
        if (upgrade_target->portrait_id == GAIA_FORM_ID) {
            fdps_draw_text(data_fdps_all_game_text_ptr, GAIA_HP_KIT_TEXT_ID,
                           (unsigned char *) PANEL_TEXT_ORIGIN,
                           VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                           MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
            upgrade_target->hp_max =
                (short) (upgrade_target->hp_max + GAIA_MAX_HP_UP_AMOUNT);
            fdps_unit_remove_item(unit_index, item_slot);
        } else {
            fdps_draw_text(data_fdps_all_game_text_ptr,
                           UPGRADE_REFUSED_TEXT_ID,
                           (unsigned char *) PANEL_TEXT_ORIGIN,
                           VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                           MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
        }
        fdps_message_window_close();
    }

    if (use_effect == USE_EFFECT_BRANDO_DEVICE) {
        upgrade_target = fdps_get_unit_record((int) target_ids[0]);
        fdps_message_window_open((int) upgrade_target->char_id);
        if (upgrade_target->char_id == BRANDO_CHARACTER_ID) {
            if (fdps_unit_find_item_slot((int) target_ids[0],
                                         METAL_ORE_ITEM_ID) == NO_ITEM_SLOT) {
                fdps_draw_text(data_fdps_all_game_text_ptr,
                               BRANDO_NO_ORE_TEXT_ID,
                               (unsigned char *) PANEL_TEXT_ORIGIN,
                               VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                               MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
            } else {
                fdps_draw_text(data_fdps_all_game_text_ptr,
                               BRANDO_TRADE_TEXT_ID,
                               (unsigned char *) PANEL_TEXT_ORIGIN,
                               VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                               MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
                /* BOTH REMOVALS COME OUT OF THE TARGET'S BAG, and the first
                   one uses the ACTING unit's slot index: PUSH [EBP+0x18] then
                   the target id at 00026d56.  For the shipped path the two are
                   the same unit, which is why it works.
                   THE 金屬礦 IS LOOKED UP A SECOND TIME AFTER THAT REMOVAL, at
                   00026d7d, and not remembered from the test above.  Removing
                   an item compacts the eight-slot bag, so the index the first
                   lookup found can already name something else by now; reusing
                   it destroys the wrong item. */
                fdps_unit_remove_item((int) target_ids[0], item_slot);
                metal_ore_slot = fdps_unit_find_item_slot(
                    (int) target_ids[0], METAL_ORE_ITEM_ID);
                fdps_unit_remove_item((int) target_ids[0], metal_ore_slot);
                fdps_unit_add_item((int) target_ids[0],
                                   HIGH_ENERGY_CANNON_ITEM_ID);
            }
        } else {
            fdps_draw_text(data_fdps_all_game_text_ptr,
                           UPGRADE_REFUSED_TEXT_ID,
                           (unsigned char *) PANEL_TEXT_ORIGIN,
                           VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                           MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
        }
        fdps_message_window_close();
    }

    data_fdps_battle_pending_xp_credit = 0;
    death_script_count = fdps_collect_death_scripts(death_scripts);
    fdps_play_death_animation_and_mark_dead();
    fdps_run_death_scripts(unit_index, death_script_count, death_scripts);
}

/* ------------------------------------------------------------------------
 * fdps_battle_item_menu @ 000252b0
 * ---------------------------------------------------------------------- */

/* The four ring-menu entries in slot order -- up, left, right, down (menu.h) --
   and the Command.cel sub-image each one carries.  Both four-int tables are
   copied onto the stack by four MOVSD each at 000252bc and 000252c8, out of the
   read-only templates at 00024d50 (7, 4, 6, 5) and 00024d60 (four zeros).  They
   are local array initialisers rather than globals, which is how
   rebuild_info/code_layout.md classifies both templates.

   BOTH COPIES ARE MADE ONCE, IN FRONT OF THE LOOP, and the flag table is only
   ever written to 1 afterwards.  So an entry greyed out on one pass stays
   greyed out for the rest of the call even once the condition that greyed it
   has gone; re-copying the template at the top of every pass, which is what
   putting the declaration inside the loop would do, would let the hand-over
   entry come back. */
#define ITEM_MENU_SLOTS 4
#define ITEM_MENU_ENTRY_USE 0
#define ITEM_MENU_ENTRY_HAND_OVER 1
#define ITEM_MENU_ENTRY_EQUIP 2
#define ITEM_MENU_ICON_USE 7
#define ITEM_MENU_ICON_HAND_OVER 4
#define ITEM_MENU_ICON_EQUIP 6
#define ITEM_MENU_ICON_DISCARD 5

/* MOV dword ptr [EBP-0x50],0x1 at 00025340: what marks a ring entry
   unselectable in the flag table menu.c reads. */
#define ITEM_MENU_ENTRY_DISABLED 1

/* What fdps_battle_action_menu is told.  MOV dword ptr [EBP-0x8],0xffffffff at
   000252d4 seeds the accumulator, the hand-over arm stores 2 over it at
   0002585a and 0002594c, and the use arm leaves through a separate slot holding
   1 at 000256b0.

   EQUIPPING AND DISCARDING LEAVE THE ACCUMULATOR AT -1.  Neither writes it, so
   backing out of the action menu after only equipping or throwing something
   away still leaves the unit free to move and attack, while doing so after a
   hand-over spends its turn.  The obvious "every branch that changed the bag
   answers 2" costs the unit its turn for an equip (see item.h). */
#define ITEM_MENU_NOTHING_DONE (-1)
#define ITEM_MENU_TURN_SPENT 1
#define ITEM_MENU_ITEM_MOVED 2

/* Every window and cursor loop in here answers -1 for a cancel and 1 for a
   confirm (unititem.h, mapcur.h).  The 1 is also stored by hand at 000256dc,
   which is what sends the use arm back round its own loop. */
#define SELECT_CANCELLED (-1)
#define SELECT_CONFIRMED 1

/* CMP EAX,-0x1 / JZ at 00025839: what fdps_unit_add_item answers when all eight
   of the recipient's entries were occupied and it stored nothing (unititem.h).
   It is the same number as a cancel and a different fact. */
#define BAG_WAS_FULL (-1)

/* One map tile is 24 pixels square, MOV EBX,0x18 in front of every one of the
   fourteen IDIVs in this function: the cursor globals hold world pixels and
   every collector, every destination global and every unit record holds tiles.
   The divide is the signed CDQ shape, so a cursor left of or above the map
   origin truncates towards zero (gamedata.h). */
#define MAP_TILE_SIZE 0x18

/* data_fdps_map_cursor_draw_mode (gamedata.h), the overlay the cursor paints:
   0 draws nothing, 1 is the plain single-tile cursor, and an item's own
   footprint is its use_radius plus 2 -- ADD EAX,0x2 at 0002545e, on the byte
   at ITEM.DAT +0x12.

   THE OVERLAY IS SWITCHED OFF ACROSS EVERY fdps_map_cursor_move_to_unit CALL
   and put back to 1 afterwards (00025663 / 00025679, 000256bc / 000256d2), so
   the cursor does not paint itself along the tiles it snaps over.  That is also
   what makes the move instant: fdps_map_cursor_move_to skips its per-step frame
   while the mode is 0 (mapcur.h). */
#define CURSOR_OVERLAY_OFF 0
#define CURSOR_OVERLAY_PLAIN 1
#define CURSOR_FOOTPRINT_BIAS 2

/* The adjacency probe, PUSH 0x3 / PUSH 0x1 / PUSH 0x1 / PUSH 0x0 at 000252f6
   and the same three numbers with a real buffer at 0002572f: one tile of reach,
   a minimum distance of 1 that drops the acting unit's own tile, and select
   mode 3, which keeps side 2 -- the player's party (aitarget.h).  The same 3 is
   handed to fdps_map_cursor_select_loop at 00025782 as its select mode, where
   it is passed through to the area collector as the test a confirm has to
   satisfy (mapcur.h).

   THE FIRST PROBE PASSES A NULL BUFFER, so it only counts; the hand-over arm
   runs the identical sweep again with somewhere to write. */
#define NEIGHBOUR_PROBE_RANGE 1
#define NEIGHBOUR_PROBE_MIN_DIST 1
#define NEIGHBOUR_SELECT_MODE 3

/* PUSH 0x1 at 00025476 / MOV byte ptr [EBP-0x4],0x0 at 0002547c: the minimum
   distance the first target sweep runs with.  It is exclusive, so 1 drops the
   acting unit's own tile and 0 keeps it. */
#define AIM_SWEEP_KEEP_OWN_TILE 0
#define AIM_SWEEP_DROP_OWN_TILE 1

/* CMP dword ptr [EBP-0x34],0xf / JLE at 00025510.  Bit 0x10 of the ITEM.DAT
   use_distance byte marks the straight-line shape and the low nibble is the
   reach, so 0x10 is the lowest line-shaped value and a line's length is the
   byte less 0x10 (aitarget.h).  PUSH 0x1 at 00025516 is that collector's
   select_enemy_side, which runs the opposite way round from a select mode: 1
   keeps side 0, the enemy side. */
#define ITEM_USE_DISTANCE_LINE_BIT 0x10
#define LINE_SWEEP_KEEPS_ENEMY_SIDE 1

/* The two use-effect codes that make this menu ask for a DESTINATION tile on
   top of the target it already has, CMP EAX,0x1c at 000255e9 and CMP EAX,0x19
   at 000255f9; 0x1c is also the only code that drops the acting unit's own tile
   from the first sweep, CMP EAX,0x1c at 00025471.

   NO SHIPPED ITEM REACHES EITHER.  All 251 ITEM.DAT records were counted and
   the use_effect byte at +0x0d never holds 0x19 or 0x1c, which is also what the
   guide's K1 list says of both codes.  fdps_apply_item_effect_to_targets has no
   arm for them either, so the destination this arm picks is written to globals
   whose only reader is the spell path (gamedata.h) and the use itself then does
   nothing.  The branch is emitted because the original runs it, not because
   anything in the shipped data can get into it. */
#define USE_EFFECT_DEST_TILE_19 0x19
#define USE_EFFECT_DEST_TILE_1C 0x1c

/* PUSH 0x6 at 00025622: fdps_map_cursor_select_loop's movement-destination
   mode.  On that mode the middle argument is NOT a list length -- it carries
   the ACTING UNIT'S INDEX, and the list pointer is NULL (mapcur.h).  What is
   handed over is targets[0], read as one unsigned byte at 0002561b, so the
   acting unit has to be the first entry the sweep above collected. */
#define DESTINATION_SELECT_MODE 6

/* PUSH 0x64 at 00025722: the candidate buffer the hand-over sweep is given, a
   hundred bytes from malloc, freed again at 000257b0 before the arm does any
   work.  Nothing tests the pointer. */
#define HAND_OVER_CANDIDATE_BYTES 100

/* The on-stack candidate buffer the use arm collects into, at [EBP-0x98].  Its
   52 bytes are the distance up to the icon table that follows it at [EBP-0x64];
   the collectors are given no capacity and write one byte per unit they match
   (aitarget.h), so the figure is a contract with them and not a preference. */
#define TARGET_BUFFER_BYTES 52

/* 000252b0.  See item.h for what the four entries do and what the answer means.

   THE WHOLE BODY IS ONE LOOP AROUND THE RING MENU, JMP 0x000252db at 000259a7,
   and every arm that finishes without spending the turn falls back into it.
   The bag is re-counted at the top of every pass and an empty bag is what ends
   the call with whatever the accumulator holds.

   ONE STACK SLOT, [EBP-0xc], IS BOTH THE RING ENTRY AND THE BAG ROW.  It is
   seeded by fdps_menu_find_first_enabled_entry, steered by the ring's cursor
   loop, tested to pick the arm, and then handed to fdps_unit_item_select_window
   as the row that window's cursor starts on.  The hand-over and discard arms
   store 0 into it first (000256fc, 00025969) and THE USE ARM DOES NOT: on its
   first pass the value is already 0, because that is the entry that chose the
   arm, but on every later pass through its own loop the item list reopens on
   THE ROW THE PLAYER LAST PICKED.  The window's opening draw still highlights
   row 0 and the bar jumps to the seeded row on the first pass of its cursor
   loop (unititem.h), so seeding 0 here like the other two arms would change
   both where the bar lands and what the player sees on the way.

   THE CURSOR'S PIXEL POSITION IS SAVED ONCE THE RING HAS CLOSED, at 0002539a,
   and it is the acting unit's tile because that is where the action menu left
   the cursor.  Both arms that move the cursor read it back: the line sweep
   sweeps FROM it, and the hand-over arm walks the cursor back to it.

   TWO CALLS COME BACK WITH A VALUE THAT IS NOT LOOKED AT.  fdps_unit_add_item's
   answer is tested -- that is what picks the swap -- but the second one made in
   the swap is not, and the fdps_get_unit_record at 00025602 stores its record
   pointer into [EBP-0x1c] and no instruction ever loads it.  The call is kept
   because the original makes it. */
int fdps_battle_item_menu(int unit_index)
{
    /* The two four-int tables the ring menu is described by, in slot order up,
       left, right, down. */
    int menu_icons[ITEM_MENU_SLOTS] = {
        ITEM_MENU_ICON_USE, ITEM_MENU_ICON_HAND_OVER,
        ITEM_MENU_ICON_EQUIP, ITEM_MENU_ICON_DISCARD
    };
    int menu_disabled[ITEM_MENU_SLOTS] = { 0, 0, 0, 0 };
    /* Where the target sweeps write the unit indices they match, one byte
       each. */
    unsigned char targets[TARGET_BUFFER_BYTES];
    /* The ring entry the cursor is on, and afterwards the bag row the item
       window's cursor is on -- one slot for both, see the note above. */
    int selected_entry;
    /* What the last window or cursor loop answered: a cancel or a confirm. */
    int pick_result;
    /* What the call will hand back once the player leaves the menu. */
    int result;
    /* How many of the acting unit's own side are standing next to it, which is
       what decides whether the hand-over entry can be chosen. */
    int adjacent_count;
    /* How many units the effect about to be used actually covers. */
    int target_count;
    /* The item the player picked out of the bag, and its ITEM.DAT record. */
    int item_id;
    struct fdps_item_effect *item;
    /* That record's reach-and-shape byte, widened to the int the signed
       compare against 0xf is made on. */
    int use_distance;
    /* Whether the first target sweep leaves the acting unit's own tile out. */
    unsigned char skip_own_tile;
    /* Where the cursor stood when the ring menu closed, in world pixels. */
    int saved_cursor_x;
    int saved_cursor_y;
    /* The hundred-byte buffer the hand-over sweep collects into. */
    unsigned char *candidates;
    /* Who the player picked to hand the item to. */
    int recipient;
    /* The item that leaves the acting unit's bag, and, when the recipient's bag
       was full and the two are traded instead, the one that comes back. */
    int given_item_id;
    int received_item_id;
    /* Which row of the acting unit's bag the given item sat in, kept across the
       second window because that window overwrites the shared slot. */
    int donor_slot;

    result = ITEM_MENU_NOTHING_DONE;

    for (;;) {
        if (fdps_unit_item_count(unit_index) == 0) {
            return result;
        }

        adjacent_count = fdps_collect_targets_in_range(
            data_fdps_map_cursor_world_x / MAP_TILE_SIZE,
            data_fdps_map_cursor_world_y / MAP_TILE_SIZE,
            NULL, NEIGHBOUR_PROBE_RANGE, NEIGHBOUR_PROBE_MIN_DIST,
            NEIGHBOUR_SELECT_MODE);
        fdps_map_grid_reset();
        if (adjacent_count == 0) {
            menu_disabled[ITEM_MENU_ENTRY_HAND_OVER] = ITEM_MENU_ENTRY_DISABLED;
        }

        selected_entry = fdps_menu_find_first_enabled_entry(menu_disabled);
        fdps_menu_animate_open(menu_icons, menu_disabled, selected_entry);
        pick_result = fdps_menu_cursor_input_loop(menu_icons, menu_disabled,
                                                  &selected_entry);
        fdps_menu_animate_close(menu_icons, menu_disabled, selected_entry);
        fdps_render_view_frame();

        saved_cursor_x = data_fdps_map_cursor_world_x;
        saved_cursor_y = data_fdps_map_cursor_world_y;

        if (pick_result == SELECT_CANCELLED) {
            return result;
        }

        if (selected_entry == ITEM_MENU_ENTRY_USE) {
            do {
                pick_result = fdps_unit_item_select_window(unit_index, 1,
                                                           &selected_entry);
                if (pick_result != SELECT_CANCELLED) {
                    item_id = (int) fdps_get_unit_record(unit_index)
                        ->inventory_slots[BAG_ENTRY_ID_INDEX(selected_entry)];
                    item = fdps_get_item_record(item_id);

                    data_fdps_map_cursor_draw_mode =
                        item->use_radius + CURSOR_FOOTPRINT_BIAS;
                    if (item->use_effect == USE_EFFECT_DEST_TILE_1C) {
                        skip_own_tile = AIM_SWEEP_DROP_OWN_TILE;
                    } else {
                        skip_own_tile = AIM_SWEEP_KEEP_OWN_TILE;
                    }
                    use_distance = (int) item->use_distance;

                    /* Which tiles the player may aim at, and then the aim. */
                    target_count = fdps_collect_targets_in_range(
                        data_fdps_map_cursor_world_x / MAP_TILE_SIZE,
                        data_fdps_map_cursor_world_y / MAP_TILE_SIZE,
                        targets, use_distance, (int) skip_own_tile,
                        (int) item->select_mode);
                    pick_result = fdps_map_cursor_select_loop(
                        (int) item->select_mode, target_count, targets);
                    fdps_map_grid_reset();
                    data_fdps_map_cursor_draw_mode = CURSOR_OVERLAY_PLAIN;

                    /* The aim is settled; collect the units the effect really
                       covers from where the cursor ended up.  THIS SWEEP RUNS
                       EVEN ON A CANCELLED AIM, and it is the count and the list
                       the apply below would be given. */
                    if (use_distance < ITEM_USE_DISTANCE_LINE_BIT) {
                        target_count = fdps_collect_targets_in_range(
                            data_fdps_map_cursor_world_x / MAP_TILE_SIZE,
                            data_fdps_map_cursor_world_y / MAP_TILE_SIZE,
                            targets, (int) item->use_radius,
                            AIM_SWEEP_KEEP_OWN_TILE, (int) item->select_mode);
                    } else {
                        target_count = fdps_collect_targets_in_line(
                            data_fdps_map_cursor_world_x / MAP_TILE_SIZE,
                            data_fdps_map_cursor_world_y / MAP_TILE_SIZE,
                            targets, saved_cursor_x / MAP_TILE_SIZE,
                            saved_cursor_y / MAP_TILE_SIZE,
                            use_distance - ITEM_USE_DISTANCE_LINE_BIT,
                            LINE_SWEEP_KEEPS_ENEMY_SIDE);
                    }
                    fdps_map_grid_reset();

                    if (item->use_effect == USE_EFFECT_DEST_TILE_1C
                        || item->use_effect == USE_EFFECT_DEST_TILE_19) {
                        /* The record this resolves is never read; see the note
                           on the function. */
                        fdps_get_unit_record(unit_index);

                        if (pick_result != SELECT_CANCELLED) {
                            pick_result = fdps_map_cursor_select_loop(
                                DESTINATION_SELECT_MODE, (int) targets[0],
                                NULL);
                        }
                        if (pick_result != SELECT_CANCELLED) {
                            data_fdps_battle_teleport_dest_tile_x =
                                data_fdps_map_cursor_world_x / MAP_TILE_SIZE;
                            data_fdps_teleport_destination_tile_y =
                                data_fdps_map_cursor_world_y / MAP_TILE_SIZE;
                            data_fdps_map_cursor_draw_mode = CURSOR_OVERLAY_OFF;
                            fdps_map_cursor_move_to_unit(unit_index);
                            data_fdps_map_cursor_draw_mode =
                                CURSOR_OVERLAY_PLAIN;
                        }
                    }

                    if (pick_result != SELECT_CANCELLED) {
                        fdps_apply_item_effect_to_targets(unit_index,
                                                          selected_entry,
                                                          target_count,
                                                          targets);
                        fdps_battle_mark_unit_done(unit_index);
                        return ITEM_MENU_TURN_SPENT;
                    }

                    /* The aim was backed out of: put the cursor on the acting
                       unit again and reopen the item list. */
                    data_fdps_map_cursor_draw_mode = CURSOR_OVERLAY_OFF;
                    fdps_map_cursor_move_to_unit(unit_index);
                    data_fdps_map_cursor_draw_mode = CURSOR_OVERLAY_PLAIN;
                    pick_result = SELECT_CONFIRMED;
                }
            } while (pick_result == SELECT_CONFIRMED);
        } else if (selected_entry == ITEM_MENU_ENTRY_HAND_OVER) {
            selected_entry = 0;
            pick_result = fdps_unit_item_select_window(unit_index, 0,
                                                       &selected_entry);
            if (pick_result != SELECT_CANCELLED) {
                candidates = (unsigned char *)
                    malloc((size_t) HAND_OVER_CANDIDATE_BYTES);
                adjacent_count = fdps_collect_targets_in_range(
                    data_fdps_map_cursor_world_x / MAP_TILE_SIZE,
                    data_fdps_map_cursor_world_y / MAP_TILE_SIZE,
                    candidates, NEIGHBOUR_PROBE_RANGE, NEIGHBOUR_PROBE_MIN_DIST,
                    NEIGHBOUR_SELECT_MODE);
                data_fdps_map_cursor_draw_mode = CURSOR_OVERLAY_PLAIN;
                pick_result = fdps_map_cursor_select_loop(NEIGHBOUR_SELECT_MODE,
                                                          adjacent_count,
                                                          candidates);

                /* Who was under the cursor, and the tidying up: all five of
                   these run whether the pick was confirmed or cancelled. */
                recipient = fdps_battle_find_unit_at_cursor();
                fdps_map_grid_reset();
                fdps_map_cursor_move_to(saved_cursor_x, saved_cursor_y);
                free(candidates);
                fdps_flush_keyboard_queue();

                if (pick_result != SELECT_CANCELLED) {
                    given_item_id = (int) fdps_get_unit_record(unit_index)
                        ->inventory_slots[BAG_ENTRY_ID_INDEX(selected_entry)];

                    if (fdps_unit_add_item(recipient, given_item_id)
                            == BAG_WAS_FULL) {
                        /* The recipient's bag was full, so the two units trade
                           instead and the player picks what comes back. */
                        donor_slot = selected_entry;
                        selected_entry = 0;
                        pick_result = fdps_unit_item_select_window(
                            recipient, 0, &selected_entry);
                        if (pick_result != SELECT_CANCELLED) {
                            received_item_id =
                                (int) fdps_get_unit_record(recipient)
                                ->inventory_slots
                                    [BAG_ENTRY_ID_INDEX(selected_entry)];
                            fdps_unit_remove_item(recipient, selected_entry);
                            fdps_unit_add_item(recipient, given_item_id);
                            fdps_unit_remove_item(unit_index, donor_slot);
                            fdps_unit_add_item(unit_index, received_item_id);
                            fdps_unit_recompute_combat_stats(recipient);
                            fdps_unit_recompute_combat_stats(unit_index);
                            result = ITEM_MENU_ITEM_MOVED;
                        }
                    } else {
                        fdps_unit_remove_item(unit_index, selected_entry);
                        fdps_unit_recompute_combat_stats(unit_index);
                        result = ITEM_MENU_ITEM_MOVED;
                    }
                }
            }
        } else if (selected_entry == ITEM_MENU_ENTRY_EQUIP) {
            fdps_unit_equip_window(unit_index);
        } else {
            selected_entry = 0;
            pick_result = fdps_unit_item_select_window(unit_index, 0,
                                                       &selected_entry);
            if (pick_result != SELECT_CANCELLED) {
                fdps_unit_remove_item(unit_index, selected_entry);
            }
            /* OUTSIDE THE TEST, at 0002599b: the derived stats are rebuilt even
               when the player backed out without discarding anything. */
            fdps_unit_recompute_combat_stats(unit_index);
        }
    }
}
