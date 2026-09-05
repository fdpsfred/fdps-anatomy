/* death.c -- the death-script pipeline: collecting what the units just killed
 * owe, playing the destruction sequence, and paying the collected records out.
 *
 * See death.h for what each entry point is asked and what its answer means,
 * and for why the collect has to run before the animation.  Nothing here owns
 * state: the battle units are reached through unit.h and their count through
 * gamedata.h.
 *
 * memmove comes from <string.h>, malloc and free from <stdlib.h> and inp from
 * <conio.h>, which is where Watcom 10.0a declares each of them, and all four
 * are real calls in the original -- CALL 0x0003d514 at 000261fd, CALL
 * 0x0003d375 at 0001d804, CALL 0x0003d478 at 0001d977 and CALL 0x0003d4e4 at
 * 0001d914 -- because the flag set carries no -oi (rebuild_info/
 * build_flags.md), so the plain declarations are what reproduce them.
 */
#include <string.h>
#include <stdlib.h>
#include <conio.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "unit.h"
#include "vfs.h"
#include "saf.h"
#include "sprite.h"
#include "blit.h"
#include "mapdraw.h"
#include "death.h"

/* How wide one collected record is: PUSH 0x3 at 000261f2 is memmove's byte
   count, and LEA EAX,[EAX+EAX*0x2] at 000261fe scales the output index by the
   same three.  It is the opcode byte at record offset 0x31 followed by the
   signed 16-bit operand at 0x32, copied as three raw bytes rather than field
   by field. */
#define DEATH_SCRIPT_RECORD_BYTES 3

/* The "no script" sentinel: MOV AL,byte ptr [EAX+0x31] / AND EAX,0xff / CMP
   EAX,0xff at 000261d5.  The byte is widened without sign before the compare,
   so this is an equality against 255 and not a test for a negative opcode; an
   opcode of 0x80 is a real script and is collected. */
#define DEATH_SCRIPT_NONE 0xff

/* 00026180.  One walk over the battle unit array with three rejections and a
   packed append.

   The bound is data_fdps_map_unit_count compared with CMP EAX,[0x00060150] /
   JL at 0002619d, so index and count are both signed and the test runs before
   the body: a count of 0 or below returns 0 without resolving a record.

   The three tests each branch to the same place, the loop's increment at
   00026213, which is what the three continues below are.  In order: AND AL,0x1
   / TEST EAX,EAX / JNZ at 000261c7 on the flags byte at record offset 5 -- bit
   0 alone, so bit 7, the acted-this-turn flag, does not disqualify anyone; the
   sentinel compare at 000261d5; and CMP word ptr [EAX+0x40],0x0 / JLE at
   000261e9 on the hit-point word.  That last one is JLE and not JL, so a unit
   sitting at exactly zero HP qualifies, and it is the signed compare over a
   signed word, so a unit driven below zero qualifies as well -- reading
   hp_current unsigned turns -1 into 65535 and loses every overkill.

   The retirement test reads the flag bit inline rather than calling
   fdps_unit_is_retired; there is no CALL between the record lookup and the
   sentinel compare.

   The record pointer comes back from CALL 0x0002d210 into [EBP-0x8] and is
   re-resolved on every iteration rather than stepped by 0x50, which is what
   keeps the walk correct across an array that has moved.

   The append destination is out_scripts + script_count * 3 -- the OUTPUT
   counter, not the unit index -- so the records land packed from the front
   with no gaps whatever spread of units qualified.  Nothing bounds that write;
   see death.h for why the missing check stays missing. */
int fdps_collect_death_scripts(unsigned char *out_scripts)
{
    struct fdps_unit_record *unit;
    int unit_index;
    int script_count;

    script_count = 0;
    for (unit_index = 0; unit_index < data_fdps_map_unit_count; unit_index++) {
        unit = fdps_get_unit_record(unit_index);
        if ((unit->flags & 1) != 0) {
            continue;
        }
        if (unit->death_script_opcode == DEATH_SCRIPT_NONE) {
            continue;
        }
        if (unit->hp_current > 0) {
            continue;
        }
        memmove(out_scripts + script_count * DEATH_SCRIPT_RECORD_BYTES,
                &unit->death_script_opcode, DEATH_SCRIPT_RECORD_BYTES);
        script_count = script_count + 1;
    }
    return script_count;
}

/* The narrow opcode window this collector accepts, inclusive at both ends:
   CMP EAX,0x2 / JGE at 00027549 and CMP EAX,0x5 / JLE at 0002755b.  Both
   compares run on the opcode byte after AND EAX,0xff, so the widening carries
   no sign and 0xff, the "no script" sentinel, falls outside the window without
   needing a test of its own.

   2, 3, 4 and 5 are exactly the opcodes fdps_run_death_scripts executes with
   no further question -- the chapter-event call, the scripted line, and the
   two battle-end verdicts.  0 and 1, the item and the gold, are the two it
   guards on the unit index it was handed, and that guard's failure is a return
   out of the whole executor rather than a skip of the one record. */
#define DEATH_EVENT_OPCODE_MIN 2
#define DEATH_EVENT_OPCODE_MAX 5

/* 000274e0.  The same walk as fdps_collect_death_scripts with the sentinel
   test replaced by the window above, so the map AI's spell kills pay out the
   scripted chapter events and battle-end verdicts and nothing else.

   The bound is data_fdps_map_unit_count read through CMP EAX,[0x00060150] / JL
   at 000274fd, signed and tested before the body, so a count of 0 or below
   returns 0 without resolving a record.

   The four rejections all branch to the loop's increment at 0002750a, which
   is what the four continues below are.  In assembly order: AND AL,0x1 / TEST
   EAX,EAX / JNZ at 00027527 on bit 0 of the flags byte at record offset 5 --
   bit 0 alone, so bit 7, the acted-this-turn flag, disqualifies nobody; CMP
   word ptr [EAX+0x40],0x0 / JLE at 00027535 on the hit-point word, JLE and not
   JL so a unit resting at exactly zero HP qualifies, and a signed compare over
   a signed word so an overkilled unit qualifies too -- reading hp_current
   unsigned turns -1 into 65535 and loses every unit the fight took past zero;
   and then the two ends of the opcode window.

   The record pointer comes back from CALL 0x0002d210 into [EBP-0x8] and is
   re-resolved every iteration rather than stepped by 0x50, so the walk stays
   correct across an array that has moved.

   The append destination is out_events + event_count * 3 -- the OUTPUT
   counter, not the unit index -- so the records land packed from the front
   with no gaps whatever spread of units qualified.  Nothing bounds that write;
   see death.h for why the missing check stays missing. */
int fdps_collect_death_script_events(unsigned char *out_events)
{
    struct fdps_unit_record *unit;
    int unit_index;
    int event_count;

    event_count = 0;
    for (unit_index = 0; unit_index < data_fdps_map_unit_count; unit_index++) {
        unit = fdps_get_unit_record(unit_index);
        if ((unit->flags & 1) != 0) {
            continue;
        }
        if (unit->hp_current > 0) {
            continue;
        }
        if (unit->death_script_opcode < DEATH_EVENT_OPCODE_MIN) {
            continue;
        }
        if (unit->death_script_opcode > DEATH_EVENT_OPCODE_MAX) {
            continue;
        }
        memmove(out_events + event_count * DEATH_SCRIPT_RECORD_BYTES,
                &unit->death_script_opcode, DEATH_SCRIPT_RECORD_BYTES);
        event_count = event_count + 1;
    }
    return event_count;
}

/* How many dying units the list has room for.  The frame runs from EBP-0x18c
   (SUB ESP,0x18c at 0001d6c6) up to the draw request at EBP-0x4c, which is
   0x140 bytes, and the append at 0001d733 scales its index by four: 80 slots.
   NOTHING BOUNDS THE APPEND -- a battle that put more than 80 units at exactly
   zero hit points in one action would write past the list -- and no check is
   added here, because the original has none. */
#define DYING_UNIT_SLOTS 80

/* Thirteen spin frames, CMP dword ptr [EBP-0x1c],0xd / JL at 0001d75d, and the
   four facings the step number is reduced to by MOV EBX,0x4 / CDQ / IDIV EBX
   at 0001d7a2.  0 is down, 1 left, 2 up, 3 right, so the unit turns on the
   spot; the last step is 12, which leaves every dying unit facing down. */
#define DEATH_SPIN_FRAMES 13
#define DEATH_FACING_COUNT 4

/* The member of the resident BaseAni.vfs image the explosion is played from,
   MOV EAX,0x6176c at 0001d821.  It is looked up straight through
   fdps_vfs_image_get_entry rather than through fdps_baseani_get_entry_or_exit,
   so a miss comes back as a NULL image and not as an exit, and the literal is
   upper-cased IN PLACE by that lookup: it has to live in writable storage
   (rebuild_info/pitfalls.md). */
#define EXPLOSION_MEMBER "Explo.Saf"

/* The page the explosion is composed on: 360 x 240 8bpp, PUSH 0x15180 / CALL
   malloc at 0001d7ff with the pitch and rows stored at 0001d80f and 0001d816.
   The 24-pixel apron on all four sides is room the sprite may hang over
   without reaching the presented window. */
#define DEATH_PAGE_PITCH 0x168
#define DEATH_PAGE_ROWS 0xf0
#define DEATH_PAGE_BYTES 0x15180
#define DEATH_PAGE_WINDOW_AT 0x21d8

/* What is presented and where: 312 x 192 taken from page byte 0x21d8, which is
   page pixel (24,24), and put down at screen byte 0x504, which is screen pixel
   (4,4).  Both are hard-coded in the original -- PUSH 0x140 at 0001d93b and
   PUSH 0xa0504 at 0001d940 -- and stay literals here: 0xa0000 is where the
   display adapter answers, not the address of anything the linker places. */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140
#define DEATH_WINDOW_AT 0x504
#define DEATH_WINDOW_W 0x138
#define DEATH_WINDOW_H 0xc0

/* VGA input status register 1.  Bit 3 is set while the vertical retrace is in
   progress, which is what each present straddles. */
#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* A map tile is 24 pixels square (IMUL EAX,EAX,0x18 at 0001d8bf and 0001d8d6),
   and the sprite is lifted six pixels above the tile's top edge (SUB EAX,0x6 at
   0001d8df) so it sits over the figure rather than over its feet. */
#define MAP_TILE_SIZE 0x18
#define DEATH_SPRITE_LIFT 6

/* 0001d6c0.  Four walks over the same list and one branch that ends the
   function early, so cyclomatic complexity 9 counting the two retrace spins
   and the tick wait.  No argument is pushed at any of the seven call sites and
   none of them cleans the stack or reads EAX afterwards -- 00012fe9, 00013dc1,
   00015fbe, 0001fb3f, 00026dd8, 0002809d and 00039e5b -- so this takes nothing
   and returns nothing.

   THE HIT-POINT TEST IS AN EQUALITY, NOT THE COLLECTORS' <= 0.  CMP word ptr
   [EAX+0x40],0x0 / JZ at 0001d727: a unit driven below zero owes its death
   script to fdps_collect_death_scripts above but is NOT destroyed here.
   Writing the obvious `<= 0`, which is what the two walks earlier in this file
   do, destroys units the original leaves standing.

   THE MARK IS A WHOLE-BYTE STORE.  MOV byte ptr [EAX+0x5],0x1 at 0001d7f9
   writes the flags byte outright, so bit 7 -- the acted-this-turn flag
   fdps_map_actor_behavior_step ORs in at 00010745 -- and everything else in
   that byte go with it.  Spelling it `flags |= 1` leaves those bits standing
   (rebuild_info/pitfalls.md).

   THERE IS NO SEPARATE PAGE VARIABLE.  The page pointer is slot 0 of the draw
   request: malloc's answer is stored there at 0001d80c and every later use --
   the compositor's argument at 0001d871, the blit source at 0001d94a and
   free's argument at 0001d973 -- reads that same slot back.

   THE FRAME WAIT'S LATCH IS DELIBERATELY LEFT UNINITIALISED, the same contract
   fdps_animate_turn_banner and fdps_play_attack_animation (anim.c) carry.
   last_tick is read at 0001d95b before anything has written it, so the first
   explosion frame does not wait for a tick.  Declaring it initialised from the
   counter, which is what writing the loop cleanly in C invites, costs one
   extra tick of animation (rebuild_info/pitfalls.md).

   data_fdps_timer_tick_counter is volatile at its declaration (gamedata.h)
   because of that wait: nothing inside it writes the counter, so a build
   allowed to hoist the load would spin here forever.  The retrace spins read a
   port and cannot be hoisted for the same reason.

   THE PRESENT IS OPEN-CODED AND NOT A CALL to fdps_render_view_frame, although
   the spin phase above uses that function and the two do the same four things.
   The shipped image has no CALL there -- 0001d90f to 0001d96e is the retrace
   pair, the blit and the tick wait written out -- and the two are not the same
   present in any case: this one has no palette cycle between the spins.

   THREE CALLS' ANSWERS ARE READ.  fdps_get_unit_record's is the record every
   field below comes out of, at all four walks; fdps_vfs_image_get_entry's is
   the sheet, stored into the request at 0001d835 and read straight back as
   fdps_saf_frame_count's argument at 0001d846; malloc's is the page.  inp's is
   tested for bit 3 at both spins.  fdps_render_view_frame,
   fdps_draw_scene_layers, fdps_draw_composite_sprite, fdps_blit_rect and free
   return nothing the original reads.

   The frames are paced by the retrace and by the timer tick, so how many
   instructions stand between them is not observable (contract D). */
void fdps_play_death_animation_and_mark_dead(void)
{
    /* Which battle units this call is destroying, by unit index. */
    int dying_units[DYING_UNIT_SLOTS];
    /* The nine-slot block sprite.h describes.  Slot 0 is the composite page --
       see the note above -- and slot 6 is the frame this pass draws. */
    int request[DRAW_REQUEST_DWORDS];
    /* The record the walk is on.  One local in the original: all four lookups
       store to [EBP-0xc]. */
    struct fdps_unit_record *unit;
    /* How many slots of dying_units are filled. */
    int dying_count;
    /* Which unit the sweep is on. */
    int unit_index;
    /* Which slot of dying_units the three later walks are on.  The original
       keeps this and unit_index above in the one frame slot at [EBP-0x28];
       nothing is carried between the walks, and frame allocation is not part
       of the standard (ADR-0001). */
    int dying_slot;
    /* Which of the thirteen spin frames this pass is. */
    int spin_frame;
    /* Which frame of the explosion sheet this pass draws. */
    int frame;
    /* The size the sheet lookup writes out, thrown away, and then the sheet's
       frame count.  It is one local in the original, [EBP-0x8]: the address
       handed over at 0001d81d is overwritten at 0001d852 before it is read. */
    unsigned int frame_count;
    /* Set only for the first unit of the list, so the frame's sound effect
       fires once per frame rather than once per dying unit. */
    char play_sound;
    /* The tick the previous frame ended on.  Deliberately not initialised --
       see the note above. */
    unsigned int last_tick;
    /* The tile the sweep read off each record at 0001d6fe and 0001d708.  Both
       are written on every unit and neither is ever read: the build is -od,
       which does not delete a dead store (rebuild_info/build_flags.md), so
       they are what the source said and not what the optimiser left. */
    int swept_tile_x;
    int swept_tile_y;

    dying_count = 0;
    for (unit_index = 0; unit_index < data_fdps_map_unit_count; unit_index++) {
        unit = fdps_get_unit_record(unit_index);
        swept_tile_x = (int) unit->pos_x;
        swept_tile_y = (int) unit->pos_y;
        if ((unit->flags & 1) == 0 && unit->hp_current == 0) {
            dying_units[dying_count] = unit_index;
            dying_count = dying_count + 1;
        }
    }
    if (dying_count == 0) {
        return;
    }

    for (spin_frame = 0; spin_frame < DEATH_SPIN_FRAMES; spin_frame++) {
        for (dying_slot = 0; dying_slot < dying_count; dying_slot++) {
            unit = fdps_get_unit_record(dying_units[dying_slot]);
            unit->facing = (unsigned char) (spin_frame % DEATH_FACING_COUNT);
        }
        fdps_render_view_frame();
    }

    for (dying_slot = 0; dying_slot < dying_count; dying_slot++) {
        unit = fdps_get_unit_record(dying_units[dying_slot]);
        unit->flags = 1;
    }

    request[DRAW_REQUEST_DEST_BASE] = (int) malloc((size_t) DEATH_PAGE_BYTES);
    request[DRAW_REQUEST_DEST_PITCH] = DEATH_PAGE_PITCH;
    request[DRAW_REQUEST_DEST_ROWS] = DEATH_PAGE_ROWS;
    request[DRAW_REQUEST_IMAGE] = (int) fdps_vfs_image_get_entry(
        (struct fdps_vfs_image_header *)
            data_fdps_animation_baseani_archive_ptr,
        EXPLOSION_MEMBER, &frame_count);
    request[DRAW_REQUEST_BLIT_OPERAND] = 0;
    request[DRAW_REQUEST_BLIT_MODE] = 0;
    frame_count = (unsigned int)
        fdps_saf_frame_count((void *) request[DRAW_REQUEST_IMAGE]);

    for (frame = 0; frame < (int) frame_count; frame++) {
        fdps_draw_scene_layers(
            (unsigned char *) request[DRAW_REQUEST_DEST_BASE]);
        for (dying_slot = 0; dying_slot < dying_count; dying_slot++) {
            unit = fdps_get_unit_record(dying_units[dying_slot]);
            /* Both tile bytes are widened UNSIGNED -- MOV AL,byte ptr [EAX]
               then AND EAX,0xff at 0001d8b5 and 0001d8cb -- so a tile column
               past 127 is far to the right and not far to the left. */
            request[DRAW_REQUEST_X] = (int) unit->pos_x * MAP_TILE_SIZE
                                      - data_fdps_battle_view_window_origin_x;
            request[DRAW_REQUEST_Y] = (int) unit->pos_y * MAP_TILE_SIZE
                                      - data_fdps_battle_view_window_origin_y
                                      - DEATH_SPRITE_LIFT;
            play_sound = (char) (dying_slot == 0);
            request[DRAW_REQUEST_ITEM_INDEX] = frame;
            fdps_draw_composite_sprite(request, play_sound);
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* Spin until the retrace begins, so the frame that has just been
               composed is the one the monitor shows whole. */
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
            /* And until it ends, so the blit starts clear of it. */
        }
        fdps_blit_rect((unsigned int)
                           ((unsigned char *) request[DRAW_REQUEST_DEST_BASE]
                            + DEATH_PAGE_WINDOW_AT),
                       DEATH_PAGE_PITCH,
                       (void *) (VGA_SCREEN_BASE + DEATH_WINDOW_AT),
                       VGA_SCREEN_PITCH, DEATH_WINDOW_W, DEATH_WINDOW_H);
        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
    }

    free((void *) request[DRAW_REQUEST_DEST_BASE]);
}
