/* item.c -- applying an item or spell effect to a list of battle-map targets,
 * and the in-battle item menu.
 *
 * See item.h for what a caller has to know.  Nothing here owns state: the unit
 * records are reached through the index the target list carries and the popup
 * queue belongs to indicat.c.
 */
#include "fdpstype.h"
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
