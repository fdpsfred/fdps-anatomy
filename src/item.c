/* item.c -- applying an item or spell effect to a list of battle-map targets,
 * and the in-battle item menu.
 *
 * See item.h for what a caller has to know.  Nothing here owns state: the unit
 * records are reached through the index the target list carries and the popup
 * queue belongs to indicat.c.
 */
#include "fdpstype.h"
#include "anim.h"
#include "unitstat.h"
#include "indicat.h"
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
