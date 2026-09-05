/* unititem.c -- a unit's inventory and what it has equipped.
 *
 * See unititem.h.  Most of the functions here reach a record through
 * fdps_get_unit_record and work on the eight 2-byte inventory entries at
 * record offset 0x0a; fdps_unit_can_equip_item asks instead whether the unit's
 * class is allowed to equip a given item at all, fdps_unit_find_item_slot
 * touches no record of its own -- it searches an inventory entirely through
 * the other two accessors -- and fdps_unit_item_select_loop is the modal
 * cursor the player moves over those eight entries in the unit status window.
 * fdps_unit_item_select_window and fdps_unit_equip_window are the two that put
 * that window up around the loop, the first answering which entry was picked
 * and the second equipping the picks itself.  The file owns no state.
 *
 * malloc, free and memmove come from <stdlib.h> and <string.h>, which is where
 * Watcom 10.0a declares them, and all three are real calls in the original --
 * CALL 0x0003d375 at 000259e7, CALL 0x0003d478 at 00025ae7 and CALL 0x0003d514
 * at 00025a00 -- because the flag set carries no -oi
 * (rebuild_info/build_flags.md), so the plain declarations are what reproduce
 * them.
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "audio.h"
#include "blit.h"
#include "fdpstype.h"
#include "statunit.h"
#include "statwin.h"
#include "table.h"
#include "unit.h"
#include "unititem.h"

/* How many inventory entries a unit record has: CMP dword ptr [EBP-0xc],0x8 /
   JL at 00025162.  Eight, which is the sixteen bytes of inventory_slots read
   two at a time. */
#define INVENTORY_ENTRY_COUNT 8

/* The equipped bit of an entry's flag byte: AND AL,0x40 at 00025188.  It is
   the same bit fdps_unit_recompute_combat_stats tests when it sums the
   equipment modifiers, and the test is a mask and not a compare -- an entry
   whose flag byte carries other bits alongside 0x40 is still equipped. */
#define INVENTORY_FLAG_EQUIPPED 0x40

/* The empty bit of an entry's flag byte: AND AL,0x80 at 0002528c.  Set means
   the slot holds nothing.  It is the bit fdps_unit_remove_item raises on the
   last entry -- MOV byte ptr [EAX+0x18],0x80 at 00025d09, record offset 0x0a
   plus 7 * 2 -- once it has shifted the entries above the removed one down. */
#define INVENTORY_FLAG_EMPTY 0x80

/* The two ends of the item type field the two searches accept: CMP dword ptr
   [EBP-0x8],0x15 at 000251c0 and again at 000251d2, and CMP dword
   ptr [EBP-0x8],0x27 at 000251d8.  Types 1..0x15 are the weapons and
   0x16..0x27 the armour (assets/items.md); everything above 0x27 is a
   consumable or a promotion badge and matches neither search. */
#define WEAPON_TYPE_MAX 0x15
#define ARMOR_TYPE_MAX 0x27

/* How many item type codes one PROEQU.DAT class record holds: CMP dword ptr
   [EBP-0xc],0x6 / JL at 00026032.  Six, which is the whole of struct
   fdps_class_equip_record and the stride fdps_get_class_equip_record scales
   by. */
#define CLASS_EQUIP_TYPE_COUNT 6

/* 00025140.  The equipped-slot search.  One counted loop over the eight
   inventory entries, i in [EBP-0xc] against the literal 8 with JL, and inside
   it a skip and two accept tests.

   The entry address is formed the long way -- MOV EAX,[EBP-0xc] / ADD EAX,EAX
   / ADD EAX,[EBP-0x18] / ADD EAX,0xa at 00025175 -- which is the record base
   plus 0x0a plus twice the index, so the entries are the 2-byte pairs of
   inventory_slots[] and 0x0a is that field's offset.

   The skip is AND AL,0x40 / AND EAX,0xff / TEST EAX,EAX / JZ: an entry without
   the equipped bit is passed over without the item table being touched at all.
   For an equipped entry the id byte is taken zero-extended -- MOV AL,byte ptr
   [EAX+0x1] / AND EAX,0xff at 00025196 -- so a slot holding id 0xff asks
   fdps_get_item_record for record 255 and not for record -1.

   The type is read straight off the returned pointer, XOR EAX,EAX / MOV EDX,
   [EBP-0x10] / MOV AL,byte ptr [EDX] at 000251aa, so it is byte +0x00 of the
   item record widened to int without sign.  The pointer itself is never
   compared against null and no other field of the record is read.

   want_armor is tested against zero and nothing else, CMP dword ptr
   [EBP+0x18],0x0 / JNZ at 000251b4, so any non-zero value selects the armour
   span.  The weapon accept is the pair CMP ...,0x0 / JLE skip then CMP
   ...,0x15 / JLE accept, and the armour accept is CMP ...,0x15 / JLE skip then
   CMP ...,0x27 / JLE accept; all four are the signed forms, on a value that
   came out of a byte, so the spans are closed at both ends.  The lower end of
   the weapon span is the one that carries meaning at run time: it is what
   keeps a blank ITEM.DAT tail record, which reads back type 0, from being
   reported as an equipped weapon.

   The first accepted slot returns immediately, MOV [EBP-0x4],EAX / JMP to the
   epilogue, so a unit carrying two equipped weapons yields the lower slot.
   Falling out of the loop stores 0xffffffff instead.  Neither argument is
   range checked and nothing is written. */
int fdps_unit_find_equipped_slot(int unit_index, int want_armor)
{
    struct fdps_unit_record *unit;
    struct fdps_item_effect *item;
    unsigned char *inventory_entry;
    int item_type;
    int slot_index;

    unit = fdps_get_unit_record(unit_index);

    for (slot_index = 0;
         slot_index < INVENTORY_ENTRY_COUNT;
         slot_index++) {
        inventory_entry = &unit->inventory_slots[slot_index * 2];
        if ((inventory_entry[0] & INVENTORY_FLAG_EQUIPPED) != 0) {
            item = fdps_get_item_record((int) inventory_entry[1]);
            item_type = (int) item->type;
            if (want_armor == 0) {
                if (item_type > 0 && item_type <= WEAPON_TYPE_MAX) {
                    return slot_index;
                }
            } else {
                if (item_type > WEAPON_TYPE_MAX &&
                    item_type <= ARMOR_TYPE_MAX) {
                    return slot_index;
                }
            }
        }
    }

    return -1;
}

/* 00025200.  The inventory entry's id byte, read straight out.  Straight-line
   code with no branch at all: MOV EAX,[EBP+0x14] / PUSH EAX / CALL
   fdps_get_unit_record / ADD ESP,0x4 for the record, then MOV EAX,[EBP+0x18] /
   ADD EAX,EAX / ADD EDX,EAX for twice the slot and MOV AL,byte ptr [EDX+0xb]
   for the byte.  0x0b is 0x0a, the offset of inventory_slots, plus the 1 that
   picks the id byte of the entry rather than its flag byte.

   XOR EAX,EAX before the MOV AL, so the byte is widened without sign: a slot
   holding id 0xff answers 255, which is the id fdps_get_item_record then
   indexes with, and not -1.

   Neither argument is tested and the entry's flag byte is not read, so an
   empty entry answers with whatever id byte was last left in it.  slot is
   multiplied and added with nothing in between -- keeping that unguarded is
   the point; see the header. */
int fdps_unit_get_item_id(int unit_index, int slot)
{
    struct fdps_unit_record *unit;

    unit = fdps_get_unit_record(unit_index);

    return (int) unit->inventory_slots[slot * 2 + 1];
}

/* 00025240.  How many of the eight inventory entries are occupied.  One
   counted loop, i in [EBP-0xc] against the literal 8 with JL at 00025269, and
   the running total in [EBP-0x8] cleared before the record is even resolved --
   MOV dword ptr [EBP-0x8],0x0 at 0002524c, ahead of the CALL.

   The entry address is formed exactly as the equipped search forms it, MOV
   EAX,[EBP-0xc] / ADD EAX,EAX / ADD EAX,[EBP-0x14] / ADD EAX,0xa at 00025279,
   so the entries are the 2-byte pairs of inventory_slots[] again.

   The test is AND AL,0x80 / AND EAX,0xff / TEST EAX,EAX / JNZ: a mask on the
   flag byte and not a compare, so an entry carrying other bits alongside 0x80
   still counts as empty and an entry carrying any bits BUT 0x80 -- the 0x40 of
   an equipped item, or the plain 0 of a carried one -- counts as occupied.
   The id byte at +1 is not read at all, so a slot's id says nothing about
   whether it is counted.

   Every one of the eight entries is examined; the JMP at 0002529d goes to the
   increment and not out of the loop, so an empty entry in the middle does not
   end the scan and a unit holding items above a hole answers with all of them.
   Neither the unit index nor the record pointer coming back from
   fdps_get_unit_record is checked, and nothing is written. */
int fdps_unit_item_count(int unit_index)
{
    struct fdps_unit_record *unit;
    unsigned char *inventory_entry;
    int occupied_count;
    int slot_index;

    occupied_count = 0;
    unit = fdps_get_unit_record(unit_index);

    for (slot_index = 0;
         slot_index < INVENTORY_ENTRY_COUNT;
         slot_index++) {
        inventory_entry = &unit->inventory_slots[slot_index * 2];
        if ((inventory_entry[0] & INVENTORY_FLAG_EMPTY) == 0) {
            occupied_count++;
        }
    }

    return occupied_count;
}

/* Where the item list sits inside the 320x200 status window image, and how big
   it is: PUSH 0x95 / PUSH 0x97 / PUSH 0x140 / ADD EAX,0x3b58 / PUSH 0x97 at
   00025b82.  0x3b58 is row 47 * 0x140 + column 152, so the rectangle's top-left
   corner is (0x98, 0x2f) -- the same corner fdps_draw_unit_inventory's four
   call sites all hand it (statunit.h).  The backdrop's own stride is its width,
   because it is a tight 0x97 x 0x95 copy of just that rectangle. */
#define ITEM_LIST_AT 0x3b58
#define ITEM_LIST_WIDTH 0x97
#define ITEM_LIST_ROWS 0x95

/* The pitch of the window image the list is composed into: PUSH 0x140 at both
   00025b8c and 00025bab.  A whole 320-pixel scanline, because the list is a
   rectangle of a full-screen frame and not a surface of its own. */
#define WINDOW_IMAGE_PITCH 0x140

/* The status window's idle animation is not offered here: XOR EAX,EAX / PUSH
   EAX at 00025bca.  A zero is not merely "do not animate" -- it also keeps
   fdps_unit_status_window_wait_input from drawing the rand this window would
   otherwise consume, which is visible in the CRT's random state (statunit.h). */
#define STATUS_WINDOW_NO_IDLE_ANIM 0

/* The six make codes the loop acts on: CMP dword ptr [EBP-0x20],0x48 at
   00025be0, 0x50 at 00025c11, 0x1c at 00025c45, 0x39 at 00025c4b, 0x1 at
   00025c95 and 0x53 at 00025c9b.  Everything else falls through and the list is
   simply painted again. */
#define SCANCODE_UP 0x48
#define SCANCODE_DOWN 0x50
#define SCANCODE_ENTER 0x1c
#define SCANCODE_SPACE 0x39
#define SCANCODE_ESC 0x01
#define SCANCODE_DELETE 0x53

/* The click the cursor moves with, the same asset name fdps_save_slot_select_loop
   uses.  fdps_play_sfx folds the name to upper case in place, so this literal
   ends up upper case after the first move (rebuild_info/pitfalls.md). */
#define CURSOR_MOVE_SFX "Beep.wav"

/* The two answers: MOV dword ptr [EBP-0x4],0x1 at 00025c57 and 00025c8a, and
   MOV dword ptr [EBP-0x4],0xffffffff at 00025ca1. */
#define ITEM_SELECT_CONFIRMED 1
#define ITEM_SELECT_CANCELLED (-1)

/* The mode 13h frame the window is opened over, and where the adapter answers:
   PUSH 0xfa00 / PUSH 0xa0000 / CALL memmove at 000259f2.  0xa0000 is a literal
   because it is the display adapter's real linear address under DOS/4GW and
   not the address of anything the linker places (rebuild_info/pitfalls.md,
   contract E). */
#define VGA_SCREEN_BYTES 0xfa00
#define VGA_SCREEN_BASE 0x000a0000

/* The list backdrop's size: PUSH 0x57e3 at 00025a08.  It is exactly
   ITEM_LIST_WIDTH * ITEM_LIST_ROWS, 151 * 149, so the copy is tight and its
   own stride is its width -- there is no slack row and no padding to a
   scanline. */
#define ITEM_LIST_BACKDROP_BYTES 0x57e3

/* The row the opening draw puts the selection bar on: PUSH 0x0 at 00025a5f.
   It is a literal and NOT *selected_slot, which is read for the first time by
   the cursor loop's own repaint. */
#define ITEM_LIST_OPENING_SLOT 0

/* The nine frames of the slide-in: MOV dword ptr [EBP-0x20],0x0 / CMP
   [EBP-0x20],0x9 / JL at 00025a87.  Steps 0..8, which is the whole of the four
   panels' travel including the one-pixel overshoot steps 6 and 7 settle
   through (statwin.h). */
#define OPEN_ANIM_STEP_COUNT 9

/* The cue the window opens with, the member at 0x61b2c.  It is the same
   spelling fdps_close_status_window plays on the way out, from that function's
   own copy at 0x6159c, and it has to be a plain writable literal for the same
   reason: fdps_play_sfx upper-cases the caller's storage in place, so this one
   is permanently "OPWIN1.WAV" after the first status window of the run
   (rebuild_info/pitfalls.md). */
#define STATUS_WINDOW_SOUND "OpWin1.wav"

/* 000259c0.  Puts the unit status window up around the item list and takes it
   down again.  One counted loop and no branch of any kind: every call below is
   made on every pass through the function.

   THE BACKDROP IS TAKEN BEFORE THE LIST IS PAINTED.  fdps_blit_rect copies the
   still-empty list rectangle out of the freshly loaded window image at
   00025a39, and only then does fdps_draw_unit_inventory paint the eight rows
   into that same rectangle at 00025a65.  Taking the copy after the paint --
   which is the intuitive order, snapshot the thing you are about to erase with
   -- captures the rows and the slot-0 bar as part of the backdrop, and every
   repaint inside fdps_unit_item_select_loop then lays the old highlight down
   again underneath the new one.

   THE OPENING DRAW'S SLOT IS A LITERAL 0.  *selected_slot is never read here;
   it is passed to the cursor loop and read there.  A window opened on a seed
   other than 0 therefore shows the bar on row 0 for the length of the slide-in
   and moves it on the loop's first repaint.

   The record fetched at 00025a71 is used for one byte, record +0x02, and
   nothing afterwards reads it: the store at 00025a84 is the only use of the
   value.  It is the sprite cache slot the cursor loop's own record lookup
   fetches again for itself (statunit.h), and both the call and the load are
   kept because both are work the original does.

   No CALL's answer is read other than fdps_load_status_cel_image's, the two
   malloc's, fdps_get_unit_record's and fdps_unit_item_select_loop's.  Neither
   malloc is checked against null, the loaded image is not checked either, and
   the loop's answer is returned exactly as it came back -- MOV [EBP-0x1c],EAX
   at 00025ad0 and MOV EAX,[EBP-0x4] at 00025b0d, with no test in between. */
int fdps_unit_item_select_window(int unit_index, int usable_only,
                                 int *selected_slot)
{
    /* The unit's record, fetched for the byte below and nothing else. */
    struct fdps_unit_record *unit;
    /* The 320x200 Status.cel frame every painter composes into, and the frame
       the slide-in and the close animate.  Owned and freed here. */
    unsigned char *window_image;
    /* The visible screen as it was on entry, so that closing the window can
       put a picture back.  Owned and freed here. */
    unsigned char *saved_screen;
    /* The clean copy of the list rectangle the cursor loop lays down again
       before each repaint.  Owned and freed here. */
    unsigned char *list_backdrop;
    /* Record +0x02, the unit's sprite cache slot.  Assigned and never read --
       see the note above. */
    int sprite_cache_slot;
    /* Which of the nine slide-in frames is being drawn. */
    int step;
    /* What the cursor loop answered: 1 for a confirmed slot, -1 for a
       cancel. */
    int select_result;

    fdps_play_sfx(STATUS_WINDOW_SOUND);
    window_image = (unsigned char *) fdps_load_status_cel_image();

    saved_screen = (unsigned char *) malloc((size_t) VGA_SCREEN_BYTES);
    memmove(saved_screen, (void *) VGA_SCREEN_BASE,
            (size_t) VGA_SCREEN_BYTES);

    list_backdrop =
        (unsigned char *) malloc((size_t) ITEM_LIST_BACKDROP_BYTES);
    fdps_blit_rect((unsigned int) (window_image + ITEM_LIST_AT),
                   WINDOW_IMAGE_PITCH, list_backdrop, ITEM_LIST_WIDTH,
                   ITEM_LIST_WIDTH, ITEM_LIST_ROWS);

    fdps_draw_unit_status_panel(unit_index, window_image);
    fdps_draw_unit_inventory(unit_index, ITEM_LIST_OPENING_SLOT,
                             window_image + ITEM_LIST_AT, WINDOW_IMAGE_PITCH);

    unit = fdps_get_unit_record(unit_index);
    sprite_cache_slot = (int) unit->sprite_cache_slot;

    for (step = 0; step < OPEN_ANIM_STEP_COUNT; step++) {
        fdps_draw_status_window_anim_frame(saved_screen, window_image, step);
    }

    select_result = fdps_unit_item_select_loop(unit_index, usable_only,
                                               window_image, list_backdrop,
                                               selected_slot);

    fdps_close_status_window(window_image, saved_screen);
    free(window_image);
    free(list_backdrop);
    free(saved_screen);
    return select_result;
}

/* 00025b20.  The modal cursor loop over one unit's eight inventory entries.
   Everything before the loop happens once: the record call, the byte at record
   +0x02 into [EBP-0x10], and the counted sweep that fills [EBP-0x1c].

   THE OCCUPIED COUNT IS TAKEN ONCE AND IS THE ONLY THING THE WRAP KNOWS.  The
   sweep is the same test fdps_unit_item_count applies -- MOV EAX,[EBP-0x18] /
   ADD EAX,EAX / ADD EAX,[EBP-0xc] / MOV AL,byte ptr [EAX+0xa] / AND AL,0x80 at
   00025b64 -- and it is outside the loop, so an inventory edited while the list
   is open does not move the wrap point.

   THE WRAP IS TWO COMPARES AND NOT A REMAINDER.  Up is CMP dword ptr [EAX],0x0
   / JNZ at 00025bf7, so slot 0 becomes count - 1; down is MOV EDX,[EBP-0x1c] /
   DEC EDX / CMP EDX,[EAX] at 00025c25, so count - 1 becomes 0.  Writing either
   as (*selected_slot + 1) % occupied_count divides by zero, because
   fdps_unit_equip_window opens this loop without first checking that the unit
   carries anything and the count really can be 0.  What the original does with
   a count of 0 is let the index walk outside 0..7; fdps_draw_unit_inventory
   draws no bar for such a row and nothing else reads it, and the caller tests
   fdps_unit_item_count after the loop returns.

   The paint is always both halves in the same order: fdps_blit_rect lays the
   untouched backdrop back over the list rectangle, then
   fdps_draw_unit_inventory redraws the eight rows with the bar on
   *selected_slot.  The wait that follows is handed record +0x02, the unit's
   sprite cache slot (statunit.h), and never the unit index itself.

   THE CONFIRM READS THE ID BYTE WITHOUT LOOKING AT THE FLAG BYTE.  MOV EDX,
   [EAX] / ADD EDX,EDX / ADD EDX,[EBP-0xc] / MOV AL,byte ptr [EDX+0xb] at
   00025c60 is record + 0x0a + 2 * slot + 1, so an empty entry answers with
   whatever id byte was last left in it, and the id is widened without sign.
   usable_only is a plain zero test, CMP dword ptr [EBP+0x18],0x0 at 00025c51,
   and the usability test is CMP byte ptr [EAX+0xd],0x0 on the returned item
   record -- unchecked for null, like every other reader of that table.  A
   refusal is silent: no sound, no message, straight back to the paint. */
int fdps_unit_item_select_loop(int unit_index, int usable_only,
                               unsigned char *window_image,
                               unsigned char *list_backdrop,
                               int *selected_slot)
{
    struct fdps_unit_record *unit;
    /* The ITEM.DAT record of the entry the player tried to confirm, looked up
       only on the usable_only path. */
    struct fdps_item_effect *item;
    /* Record +0x02, the slot of the sprite cache the window animates the unit
       out of.  Cached before the loop and handed to every wait. */
    int sprite_cache_slot;
    /* How many of the eight entries hold something, counted once.  It is the
       cursor's wrap point and nothing else. */
    int occupied_count;
    /* The entry being examined by the counting sweep. */
    int slot_index;
    /* The make code the last wait came back with, held signed and compared as
       a full int. */
    int scancode;
    /* The id byte of the entry being confirmed, widened without sign. */
    int item_id;

    occupied_count = 0;
    unit = fdps_get_unit_record(unit_index);
    sprite_cache_slot = (int) unit->sprite_cache_slot;

    for (slot_index = 0;
         slot_index < INVENTORY_ENTRY_COUNT;
         slot_index++) {
        if ((unit->inventory_slots[slot_index * 2]
             & INVENTORY_FLAG_EMPTY) == 0) {
            occupied_count++;
        }
    }

    for (;;) {
        fdps_blit_rect((unsigned int) list_backdrop, ITEM_LIST_WIDTH,
                       window_image + ITEM_LIST_AT, WINDOW_IMAGE_PITCH,
                       ITEM_LIST_WIDTH, ITEM_LIST_ROWS);
        fdps_draw_unit_inventory(unit_index, *selected_slot,
                                 window_image + ITEM_LIST_AT,
                                 WINDOW_IMAGE_PITCH);
        scancode = fdps_unit_status_window_wait_input(
                       window_image, sprite_cache_slot,
                       STATUS_WINDOW_NO_IDLE_ANIM);

        if (scancode == SCANCODE_UP) {
            fdps_play_sfx(CURSOR_MOVE_SFX);
            if (*selected_slot == 0) {
                *selected_slot = occupied_count - 1;
            } else {
                *selected_slot = *selected_slot - 1;
            }
        } else if (scancode == SCANCODE_DOWN) {
            fdps_play_sfx(CURSOR_MOVE_SFX);
            if (occupied_count - 1 == *selected_slot) {
                *selected_slot = 0;
            } else {
                *selected_slot = *selected_slot + 1;
            }
        } else if (scancode == SCANCODE_ENTER || scancode == SCANCODE_SPACE) {
            if (usable_only == 0) {
                return ITEM_SELECT_CONFIRMED;
            }
            item_id = (int) unit->inventory_slots[*selected_slot * 2 + 1];
            item = fdps_get_item_record(item_id);
            if (item->use_effect != 0) {
                return ITEM_SELECT_CONFIRMED;
            }
        } else if (scancode == SCANCODE_ESC || scancode == SCANCODE_DELETE) {
            return ITEM_SELECT_CANCELLED;
        }
    }
}

/* 00025cc0.  Drops one inventory entry and closes the gap.  Straight-line code
   with no branch at all -- the whole body is the record call, one memmove and
   one byte store.

   The record comes back into the only local the function has, MOV dword ptr
   [EBP-0x4],EAX at 00025cd8, and every one of the three addresses below is
   built from it.

   The three memmove arguments are pushed right to left and are computed in
   that order.  The count first: MOV EAX,0x7 / SUB EAX,[EBP+0x18] / ADD EAX,EAX
   at 00025cdb, which is (7 - slot) * 2 bytes, the entries above the removed one
   at two bytes each.  Then the source, MOV EAX,[EBP+0x18] / ADD EAX,EAX / ADD
   EAX,[EBP-0x4] / ADD EAX,0xc, which is entry slot+1; then the destination, the
   same three instructions with ADD EAX,0xa, which is entry slot.  So entries
   slot+1..7 become entries slot..6.  The two spans overlap by every entry but
   one and the copy runs upward, which is what makes this memmove and not
   memcpy.

   The count is formed as a signed subtract and pushed as it stands, so it is
   the signed value that reaches memmove's unsigned parameter: slot 7 makes it
   zero and nothing moves, and any slot above 7 makes it negative and therefore
   a huge byte count.  Neither argument is range checked anywhere in the body.

   The last store is MOV byte ptr [EAX+0x18],0x80 at 00025d09 -- record offset
   0x0a plus 7 * 2, the flag byte of the last entry -- and it is one byte wide.
   The id byte beside it at record offset 0x19 keeps whatever the memmove left
   there; see the header for why widening this store changes behaviour.

   EAX still holds the record pointer at the RET, but the function is declared
   void because no caller reads it: all 37 call sites push two arguments, CALL,
   and ADD ESP,0x8. */
void fdps_unit_remove_item(int unit_index, int slot)
{
    struct fdps_unit_record *unit;

    unit = fdps_get_unit_record(unit_index);

    memmove(&unit->inventory_slots[slot * 2],
            &unit->inventory_slots[slot * 2 + 2],
            (size_t) ((INVENTORY_ENTRY_COUNT - 1 - slot) * 2));

    unit->inventory_slots[(INVENTORY_ENTRY_COUNT - 1) * 2] =
        INVENTORY_FLAG_EMPTY;
}

/* 00025d20.  Puts one item in the first empty inventory entry.  One counted
   loop, i in [EBP-0x8] against the literal 8 with JL at 00025d42, and the
   record resolved once into [EBP-0x10] before it starts.

   The entry address is formed exactly as the other three scans form it -- MOV
   EAX,[EBP-0x8] / ADD EAX,EAX / ADD EAX,[EBP-0x10] / ADD EAX,0xa at 00025d52 --
   so the entries are the 2-byte pairs of inventory_slots[] again.

   The empty test is AND AL,0x80 / AND EAX,0xff / TEST EAX,EAX / JZ, the same
   mask fdps_unit_item_count uses and not a compare: an entry carrying other
   bits alongside 0x80 is still empty, and the JZ falls through to the increment
   at 00025d4a, so an occupied entry in the middle does not end the scan.

   Taking the slot is two stores.  MOV byte ptr [EDX],0x0 at 00025d73 puts a
   plain zero over the whole flag byte -- that is what clears the empty bit so
   the slot counts, and the equipped bit with it -- and MOV AL,byte ptr
   [EBP+0x18] / MOV byte ptr [EDX+0x1],AL at 00025d76 writes the id beside it,
   reading one byte of the pushed argument and no more.  Then MOV [EBP-0x4],0x1
   and out; nothing else in the record is touched and no entry is moved.

   Falling out of the loop stores 0xffffffff instead, and that path writes
   nothing at all.  Neither argument is range checked. */
int fdps_unit_add_item(int unit_index, int item_id)
{
    struct fdps_unit_record *unit;
    unsigned char *inventory_entry;
    int slot_index;

    unit = fdps_get_unit_record(unit_index);

    for (slot_index = 0;
         slot_index < INVENTORY_ENTRY_COUNT;
         slot_index++) {
        inventory_entry = &unit->inventory_slots[slot_index * 2];
        if ((inventory_entry[0] & INVENTORY_FLAG_EMPTY) != 0) {
            inventory_entry[0] = 0;
            inventory_entry[1] = (unsigned char) item_id;
            return 1;
        }
    }

    return -1;
}

/* Where the stats block sits inside the same 320x200 window image and how big
   it is: PUSH 0x55 / PUSH 0x7d / PUSH 0x7d / PUSH [EBP-0x4] / PUSH 0x140 /
   ADD EAX,0x8ad3 at 00025e2a.  0x8ad3 is row 111 * 0x140 + column 19, so the
   rectangle's top-left corner is (0x13, 0x6f) and it covers the HP and MP
   gauges and the stat figures fdps_draw_unit_status_panel paints (statunit.h).
   Its backdrop is a tight copy like the list's, so the backdrop's own stride
   is its width. */
#define STATS_BLOCK_AT 0x8ad3
#define STATS_BLOCK_WIDTH 0x7d
#define STATS_BLOCK_ROWS 0x55

/* The stats backdrop's size: PUSH 0x2981 at 00025df1, which is exactly
   STATS_BLOCK_WIDTH * STATS_BLOCK_ROWS, 125 * 85, with no slack row and no
   padding to a scanline. */
#define STATS_BACKDROP_BYTES 0x2981

/* The usable_only argument this menu opens the cursor loop with: PUSH 0x0 at
   00025ebd.  Zero, so a confirm accepts whatever entry the cursor is on and
   the item table is not consulted at all -- an item the unit cannot drink is
   still one it may be able to wear. */
#define EQUIP_ACCEPTS_ANY_ITEM 0

/* The cue an accepted equip plays, the member at 0x61b44 and the only use of
   that string in the image.  fdps_play_sfx folds the name to upper case in the
   caller's own storage, so this literal is permanently "EQUIP.WAV" after the
   first item is put on (rebuild_info/pitfalls.md). */
#define EQUIP_SOUND "Equip.wav"

/* 00025da0.  The equipment menu: the status window opened on one unit's bag,
   equipping whatever the player picks until they back out.

   BOTH BACKDROPS ARE TAKEN BEFORE ANYTHING IS PAINTED INTO THE IMAGE.  The two
   fdps_blit_rect calls at 00025e22 and 00025e42 copy the list rectangle and
   the stats rectangle out of the freshly loaded window image, and only then do
   fdps_draw_unit_status_panel at 00025e52 and fdps_draw_unit_inventory at
   00025e6e paint into it.  Taking either copy after the paint -- the intuitive
   order, snapshot the thing you are about to erase with -- captures the item
   rows, or the stat figures, as part of the backdrop, and every later erase
   then leaves the old highlight and the old digits showing under the new ones.

   THE LOOP HAS TWO EXITS AND BOTH ARE TESTED AFTER THE CURSOR LOOP HAS
   RETURNED.  CMP dword ptr [EBP-0x20],-0x1 / JZ at 00025f0c is the cancel, and
   fdps_unit_item_count == 0 at 00025f16 is the only way out for a unit
   carrying nothing.  The window is therefore opened, painted and slid in even
   for an empty bag, and an early "nothing to equip" guard changes what the
   player sees.  The count call is short-circuited away on a cancel: JZ jumps
   straight past it.

   THE ID BYTE IS READ ON EVERY PASS, WHICHEVER WAY THE CURSOR LOOP ENDED, and
   the read at 00025ece..00025f09 is an inline expansion of
   fdps_unit_get_item_id at 00025200 -- the caller's frame carries the two
   parameter-shaped slots at [EBP-0x2c] and [EBP-0x30] and the result slot at
   [EBP-0x24] that the expansion fingerprint is (rebuild_info/build_flags.md).
   It is emitted open-coded here rather than as a call, because a plain call
   would put a CALL in the rebuild that the original does not have; the
   expression is the same one fdps_unit_get_item_id's body computes.

   A REFUSED ITEM IS REFUSED IN SILENCE.  When fdps_unit_can_equip_item answers
   0 the JZ at 00025f36 goes straight to the JMP that starts the next pass --
   no sound, no message, the list simply comes back.

   Nothing on the accepted path repaints the item list.  Only the stats
   rectangle is erased from its backdrop and redrawn, because
   fdps_unit_item_select_loop lays the list backdrop down again at the top of
   every one of its own passes.

   No CALL's answer is read other than fdps_load_status_cel_image's, the three
   malloc's, fdps_unit_item_select_loop's, fdps_get_unit_record's,
   fdps_unit_item_count's and fdps_unit_can_equip_item's.  No malloc is checked
   against null and the loaded image is not checked either. */
void fdps_unit_equip_window(int unit_index)
{
    /* The unit's record, resolved once per pass for the id byte below and for
       nothing else. */
    struct fdps_unit_record *unit;
    /* The 320x200 Status.cel frame every painter composes into, and the frame
       the slide-in and the close animate.  Owned and freed here. */
    unsigned char *window_image;
    /* The visible screen as it was on entry, so that closing the window can
       put a picture back.  Owned and freed here. */
    unsigned char *saved_screen;
    /* The clean copy of the list rectangle the cursor loop lays down again
       before each of its repaints.  Owned and freed here. */
    unsigned char *list_backdrop;
    /* The clean copy of the stats rectangle, laid back down to erase the old
       figures before the panel is redrawn.  Owned and freed here. */
    unsigned char *stats_backdrop;
    /* Which of the nine slide-in frames is being drawn. */
    int step;
    /* The entry the cursor is on.  Seeded 0 here -- MOV dword ptr
       [EBP-0x14],0x0 at 00025dac -- and owned by the cursor loop afterwards,
       which is why it survives from one pass to the next. */
    int selected_slot;
    /* What the cursor loop answered: 1 for a confirmed slot, -1 for a
       cancel. */
    int select_result;
    /* The id byte of the entry the cursor was left on, widened without
       sign. */
    int item_id;

    selected_slot = 0;
    window_image = (unsigned char *) fdps_load_status_cel_image();

    saved_screen = (unsigned char *) malloc((size_t) VGA_SCREEN_BYTES);
    memmove(saved_screen, (void *) VGA_SCREEN_BASE,
            (size_t) VGA_SCREEN_BYTES);

    list_backdrop =
        (unsigned char *) malloc((size_t) ITEM_LIST_BACKDROP_BYTES);
    stats_backdrop = (unsigned char *) malloc((size_t) STATS_BACKDROP_BYTES);

    fdps_blit_rect((unsigned int) (window_image + ITEM_LIST_AT),
                   WINDOW_IMAGE_PITCH, list_backdrop, ITEM_LIST_WIDTH,
                   ITEM_LIST_WIDTH, ITEM_LIST_ROWS);
    fdps_blit_rect((unsigned int) (window_image + STATS_BLOCK_AT),
                   WINDOW_IMAGE_PITCH, stats_backdrop, STATS_BLOCK_WIDTH,
                   STATS_BLOCK_WIDTH, STATS_BLOCK_ROWS);

    fdps_draw_unit_status_panel(unit_index, window_image);
    fdps_draw_unit_inventory(unit_index, ITEM_LIST_OPENING_SLOT,
                             window_image + ITEM_LIST_AT, WINDOW_IMAGE_PITCH);

    fdps_play_sfx(STATUS_WINDOW_SOUND);

    for (step = 0; step < OPEN_ANIM_STEP_COUNT; step++) {
        fdps_draw_status_window_anim_frame(saved_screen, window_image, step);
    }

    for (;;) {
        select_result = fdps_unit_item_select_loop(unit_index,
                                                   EQUIP_ACCEPTS_ANY_ITEM,
                                                   window_image, list_backdrop,
                                                   &selected_slot);

        unit = fdps_get_unit_record(unit_index);
        item_id = (int) unit->inventory_slots[selected_slot * 2 + 1];

        if (select_result == ITEM_SELECT_CANCELLED ||
            fdps_unit_item_count(unit_index) == 0) {
            break;
        }

        if (fdps_unit_can_equip_item(unit_index, item_id) != 0) {
            fdps_play_sfx(EQUIP_SOUND);
            fdps_unit_equip_slot(unit_index, selected_slot);
            fdps_unit_recompute_combat_stats(unit_index);
            fdps_blit_rect((unsigned int) stats_backdrop, STATS_BLOCK_WIDTH,
                           window_image + STATS_BLOCK_AT, WINDOW_IMAGE_PITCH,
                           STATS_BLOCK_WIDTH, STATS_BLOCK_ROWS);
            fdps_draw_unit_status_panel(unit_index, window_image);
        }
    }

    fdps_close_status_window(window_image, saved_screen);
    free(window_image);
    free(list_backdrop);
    free(stats_backdrop);
    free(saved_screen);
}

/* 00025fe0.  May this unit's class equip this item?  Three calls and then one
   counted loop, with no branch of any kind before the loop is entered.

   The three record calls happen in source order and each result goes to a
   stack local of its own: MOV [EBP-0x18],EAX at 00025ff8 for the unit record,
   MOV [EBP-0x10],EAX at 0002600f for the class equipment record, MOV
   [EBP-0x14],EAX at 0002601e for the item record.  None of the three pointers
   is tested for null and no argument is range checked.

   The class code is byte +0x20 of the unit record, taken zero-extended: MOV
   EAX,[EBP-0x18] / MOV AL,byte ptr [EAX+0x20] / AND EAX,0xff at 00025ffb, so
   a class code of 0x80 or above asks for record 128..255 and not for a record
   in front of the table.  It is pushed exactly as read -- the INC that every
   caller of fdps_get_class_record applies is absent here, because PROEQU.DAT
   has no leading default row (see table.h).

   The item's type is byte +0x00 of its record, also zero-extended and kept as
   an int: XOR EAX,EAX / MOV EDX,[EBP-0x14] / MOV AL,byte ptr [EDX] at
   00026021.  No other field of the item record is read.

   The loop is i in [EBP-0xc] against the literal 6 with JL at 00026032, and
   the body is MOV EAX,[EBP-0x10] / ADD EAX,[EBP-0xc] / MOV AL,byte ptr [EAX] /
   AND EAX,0xff / CMP EAX,[EBP-0x8] -- one byte per position, so the six
   positions are the six bytes of the record and the stride is 1.  A match
   stores 1 and jumps to the epilogue, so the scan stops at the first equal
   position; falling out of all six stores 0.

   There is no sentinel test anywhere in the loop: all six positions are
   compared even though the unused ones hold 0xFF, and a position holding 0 is
   compared like any other.  All four call sites use the answer as a plain
   boolean, TEST EAX,EAX right after the ADD ESP,0x8. */
int fdps_unit_can_equip_item(int unit_index, int item_id)
{
    struct fdps_unit_record *unit;
    struct fdps_class_equip_record *class_equip;
    struct fdps_item_effect *item;
    int item_type;
    int type_slot;

    unit = fdps_get_unit_record(unit_index);
    class_equip = fdps_get_class_equip_record((int) unit->clazz);
    item = fdps_get_item_record(item_id);
    item_type = (int) item->type;

    for (type_slot = 0;
         type_slot < CLASS_EQUIP_TYPE_COUNT;
         type_slot++) {
        if ((int) class_equip->allowed_item_type[type_slot] == item_type) {
            return 1;
        }
    }

    return 0;
}

/* 00026070.  Equips one inventory entry and takes off whatever the unit wore in
   the same category.  Two record calls, then one counted loop over the eight
   entries, then one unconditional store.

   The unit record is resolved TWICE, both times with the same argument: MOV
   EAX,[EBP+0x14] / PUSH EAX / CALL fdps_get_unit_record / MOV [EBP-0x1c],EAX at
   00026080, and again through the parameter slots at 000260a6 into [EBP-0x24].
   The second resolution serves only the id read below and its result is never
   used again; the loop and the final store both address [EBP-0x1c].  That
   second frame -- two parameter-shaped slots at [EBP-0x34]/[EBP-0x30] copied on
   into [EBP-0x28]/[EBP-0x2c], the body replayed against them, and the byte
   copied out of a result slot at [EBP-0x20] into [EBP-0x8] -- is the shape an
   inline expansion leaves (rebuild_info/build_flags.md).  Written open-coded it
   is the same two calls the original makes; turning it into a plain call to
   fdps_unit_get_item_id would put a CALL here that the original does not have.

   The id of the entry being equipped is MOV EDX,[EBP-0x2c] / ADD EDX,EDX / ADD
   EDX,[EBP-0x24] / XOR EAX,EAX / MOV AL,byte ptr [EDX+0xb] at 000260b1: record
   offset 0x0a plus twice the slot plus the 1 that picks the id byte, widened
   without sign, so an entry holding 0xff asks fdps_get_item_record for record
   255.  Its type is byte +0x00 of the item record, XOR EAX,EAX / MOV AL,byte
   ptr [EDX] at 000260d6, kept as an int.

   The loop is i in [EBP-0x10] against the literal 8 with JL at 000260e7, and
   the entry address is formed as the other four scans form it, MOV EAX,
   [EBP-0x10] / ADD EAX,EAX / ADD EAX,[EBP-0x1c] / ADD EAX,0xa at 000260fa.  The
   skip is AND AL,0x40 / AND EAX,0xff / TEST EAX,EAX / JZ -- a mask, so an entry
   carrying other bits alongside 0x40 is still equipped -- and the JZ goes to the
   increment at 0002615f, so the scan runs all eight entries with no early exit
   and every matching entry is unequipped, not just the first.

   The category test is four signed compares against 0x15 on two values that
   came out of bytes: CMP [EBP-0x4],0x15 / JG at 0002613f goes to the second
   pair, and the accepting paths are (both <= 0x15) at 00026145 and (both >
   0x15) at 00026151.  So the two items share a category exactly when they are
   on the same side of 0x15 -- weapons 0x01..0x15 against armour and everything
   above, not equality of the type byte and not the armour span's own 0x27 top
   end.  Ahead of it, CMP [EBP-0xc],0x0 / JZ at 00026139 leaves an equipped
   entry whose item record reads back type 0 alone; that is what keeps a blank
   ITEM.DAT tail record from being taken off as though it were a weapon.

   Unequipping is MOV byte ptr [EAX],0x0 at 0002615c -- a plain zero over the
   whole flag byte, which takes the equipped bit off and leaves the empty bit
   clear, so the entry becomes a carried item.  The id byte beside it is not
   written.

   The last store is MOV EAX,[EBP+0x18] / ADD EAX,EAX / ADD EAX,[EBP-0x1c] / MOV
   byte ptr [EAX+0xa],0x40 at 00026161: it re-reads the slot argument rather
   than a copy, it is a store of the literal and not an OR, and it is
   unconditional -- an entry the loop had just zeroed is equipped again here,
   and so is one that was marked empty.

   Nothing is range checked and neither record pointer is tested for null.  EAX
   still holds the entry address at the RET, but the function is void: both call
   sites push two arguments, CALL, ADD ESP,0x8 and go straight on to another
   MOV EAX. */
void fdps_unit_equip_slot(int unit_index, int slot)
{
    struct fdps_unit_record *unit;
    struct fdps_item_effect *equip_item;
    struct fdps_item_effect *worn_item;
    unsigned char *inventory_entry;
    int equip_item_id;
    int equip_type;
    int worn_type;
    int slot_index;

    unit = fdps_get_unit_record(unit_index);

    equip_item_id = (int)
        fdps_get_unit_record(unit_index)->inventory_slots[slot * 2 + 1];
    equip_item = fdps_get_item_record(equip_item_id);
    equip_type = (int) equip_item->type;

    for (slot_index = 0;
         slot_index < INVENTORY_ENTRY_COUNT;
         slot_index++) {
        inventory_entry = &unit->inventory_slots[slot_index * 2];
        if ((inventory_entry[0] & INVENTORY_FLAG_EQUIPPED) != 0) {
            worn_item = fdps_get_item_record((int) inventory_entry[1]);
            worn_type = (int) worn_item->type;
            if (worn_type != 0 &&
                ((equip_type <= WEAPON_TYPE_MAX &&
                  worn_type <= WEAPON_TYPE_MAX) ||
                 (equip_type > WEAPON_TYPE_MAX &&
                  worn_type > WEAPON_TYPE_MAX))) {
                inventory_entry[0] = 0;
            }
        }
    }

    unit->inventory_slots[slot * 2] = INVENTORY_FLAG_EQUIPPED;
}

/* 00034520.  Which inventory entry holds this item?  One call for the bound, a
   guard on it, and one counted loop over fdps_unit_get_item_id.  The body
   resolves no record itself and reads no byte of one.

   The bound comes first -- MOV EAX,[EBP+0x14] / PUSH EAX / CALL
   fdps_unit_item_count / ADD ESP,0x4 / MOV [EBP-0xc],EAX at 0003452c -- and is
   then guarded on its own: CMP dword ptr [EBP-0xc],0x0 / JNZ at 0003453b, with
   the zero side storing 0xffffffff and jumping straight to the epilogue.  That
   guard decides nothing the loop test would not decide anyway, since a bound of
   zero runs no iterations; it is a branch the original has and the loop below is
   entered only through it.

   The loop test is MOV EAX,[EBP-0x8] / CMP EAX,[EBP-0xc] / JL at 00034551, the
   signed form, and the value it compares against is the OCCUPIED-ENTRY COUNT and
   not the eight physical entries.  That is the whole behaviour of this function
   and it is not the loop an author would write; see the header.

   The body pushes its two arguments right to left -- MOV EAX,[EBP-0x8] / PUSH
   EAX for the entry index, then MOV EAX,[EBP+0x14] / PUSH EAX for the unit index
   at 00034563 -- so the call is fdps_unit_get_item_id(unit_index, slot).  Its
   result is used once and straight out of EAX: CMP EAX,dword ptr [EBP+0x18] /
   JNZ at 00034573.  That is a full 32-bit equality compare against the argument
   exactly as the caller pushed it, so the id is neither masked nor widened here
   and an item_id above 0xff matches nothing -- fdps_unit_get_item_id answers
   0..255.

   A match stores the entry index and jumps to the epilogue, MOV [EBP-0x4],EAX /
   JMP at 00034578, so the LOWEST matching entry is the answer and the scan does
   not run on.  Falling out of the loop stores 0xffffffff at 00034582, the same
   value the empty-inventory guard stores.  Neither argument is range checked and
   nothing is written. */
int fdps_unit_find_item_slot(int unit_index, int item_id)
{
    int occupied_count;
    int slot_index;

    occupied_count = fdps_unit_item_count(unit_index);
    if (occupied_count == 0) {
        return -1;
    }

    for (slot_index = 0; slot_index < occupied_count; slot_index++) {
        if (fdps_unit_get_item_id(unit_index, slot_index) == item_id) {
            return slot_index;
        }
    }

    return -1;
}
