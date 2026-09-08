/* chevt5.c -- the scripted chapter-event handlers of chapters 24 to 27.
 *
 * These are slots of the chapter-event handler table at 000601c4, called only
 * through it: a byte out of the loaded map file picks the slot and the
 * dispatchers -- the turn-event runner, the cell search and the death-script
 * runner -- call it indirectly, so none of them appears as a static caller.
 *
 * See chevt5.h for what each handler does.  chevt1.c is the same family for
 * chapters 2 to 7, chevt2.c for 8 to 14, chevt3.c for 15 to 19, chevt4.c for
 * 20 to 23 and chevt6.c for 28 to 30.  Nothing here owns state.
 */
#include "fdpstype.h"
#include "gamedata.h"
#include "text.h"
#include "deploy.h"
#include "unit.h"
#include "unititem.h"
#include "mapcur.h"
#include "mapdraw.h"
#include "msgwin.h"
#include "chevt5.h"

/* The destination the handler hands fdps_draw_text and the screen's row
   stride, PUSH 0xa0000 and PUSH 0x140 before each of the six draws: the
   top-left corner of the visible mode-13h page.  0xa0000 stays a literal
   because it is an address inside the display adapter's aperture rather than
   the address of anything the linker places (rebuild_info/pitfalls.md,
   contract E). */
#define CH24_TURN_TEXT_DEST 0x000a0000
#define VGA_SCREEN_PITCH 0x140

/* The standard message colours, PUSH 0xd0 / PUSH 0x0 / PUSH 0x6d before each
   draw: glyph fill, no cell background, and the shadow the outline colour
   becomes while the font's outline flag is clear. */
#define MESSAGE_FG_COLOR 0xd0
#define MESSAGE_BG_COLOR 0
#define MESSAGE_OUTLINE_COLOR 0x6d

/* The six turns the handler names, CMP dword ptr [0x00069ce8],... at 00038cd3,
   00038d17, 00038d48, 00038d8c, 00038dd0 and 00038e11.  They are tested in
   this order and not in numeric order, because it is the order MAP23.DAT's
   turn-event table lists its six live records in: (5, 8, 7, 10, 13, 15), every
   one of them routed to table slot 36.  All six tests are equalities, so the
   turn counter's signedness cannot be seen here. */
#define CH24_WAVE_2_TURN 5
#define CH24_LINE_ONLY_TURN 8
#define CH24_SHAN_ARRIVAL_TURN 7
#define CH24_WAVE_4_TURN 10
#define CH24_WAVE_3_TURN 13
#define CH24_WAVE_5_TURN 15

/* The entries of the chapter's own FDETXT24.TXT block each turn speaks, PUSH
   0x10 at 00038cef, PUSH 0x11 at 00038d33, PUSH 0xe at 00038d77, PUSH 0x12 at
   00038dbb, PUSH 0x13 at 00038dec and PUSH 0x14 at 00038e2d, out of the block
   data_fdps_current_chapter_text_ptr holds.  Turn 7's is the lowest of the six
   and is not in step with the turn order either. */
#define CH24_WAVE_2_TEXT_ID 0x10
#define CH24_LINE_ONLY_TEXT_ID 0x11
#define CH24_SHAN_ARRIVAL_TEXT_ID 0x0e
#define CH24_WAVE_4_TEXT_ID 0x12
#define CH24_WAVE_3_TEXT_ID 0x13
#define CH24_WAVE_5_TEXT_ID 0x14

/* The waves each turn brings on, PUSH 0x2 at 00038d02, PUSH 0x6 at 00038d54,
   PUSH 0x4 at 00038d98, PUSH 0x3 at 00038dff and PUSH 0x5 at 00038e40.  The
   wave number is a literal per case and is never derived from the turn: the
   turns are 5, 7, 10, 13, 15 and the waves they ask for are 2, 6, 4, 3, 5, so
   no offset from the turn produces them.  Wave 6 is MAP23.DAT's one
   player-side record, 珊 the 法師 at character id 0x0a, and the other four are
   groups of enemies. */
#define CH24_WAVE_2 2
#define CH24_SHAN_WAVE 6
#define CH24_WAVE_4 4
#define CH24_WAVE_3 3
#define CH24_WAVE_5 5

/* How every wave here is placed, XOR EAX,EAX / PUSH EAX before each of the
   five deployment calls: zero, so fdps_deploy_wave passes 0 on to
   fdps_deploy_unit and each arrival settles on the nearest free walkable tile
   to its placement record's coordinates rather than on those coordinates
   themselves. */
#define CH24_TURN_PLACE_ON_NEAREST_FREE_TILE 0

/* 00038cc0.  Chapter 24's turn-scheduled event handler: the one slot MAP23.DAT
   names for all six of the chapter's turn events, running whichever of them is
   due for the turn that has just been finished.

   The frame is the family's four-push one with no locals at all -- PUSH EBX /
   PUSH ESI / PUSH EDI / PUSH EBP / MOV EBP,ESP / SUB ESP,0x0 at
   00038cc0..00038cc6 -- so the one incoming dword sits at [EBP+0x14].  Every
   caller-clean in the body is this function's own (ADD ESP,0x1c after each
   draw and ADD ESP,0xc after each deployment), the RET at 00038e54 carries no
   immediate, and fdps_battle_run_turn_events pushes one dword and drops it
   with ADD ESP,0x4 at 0002e146, so the convention is the stack one at both
   ends of the call.

   THE CALL ORDER INSIDE A CASE IS BEHAVIOUR.  Turns 5, 13 and 15 draw and then
   deploy; turns 7 and 10 deploy and then draw.  See chevt5.h.

   THE CHAIN HAS NO DEFAULT BRANCH.  Six equality tests, each falling through a
   JNZ into the next, and the last JNZ at 00038e18 goes to the epilogue, so a
   turn none of the six names leaves the body having done nothing.  The six are
   mutually exclusive, so nothing depends on which of them is tested first.

   THE WAVE IS A LITERAL PER CASE.  Unlike the chapter 23 handler next door,
   which biases the turn counter by four to get its wave key, nothing here is
   computed from the turn -- see the wave constants above.

   THE INCOMING ARGUMENT IS NEVER READ.  MOV dword ptr [EBP+0x14],0x0 at
   00038ccc is the only access to the slot in the whole body and nothing loads
   it afterwards.  fdps_battle_run_turn_events, the only dispatcher that
   reaches this table slot, pushes a literal 0 at 0002e13e, so no value is lost
   by that.

   NO VALUE IS USED AFTER A CALL.  Each fdps_draw_text cursor is discarded --
   the next instruction is the ADD ESP that cleans its arguments -- and
   fdps_deploy_wave returns nothing.  Nothing sets EAX before the RET and no
   dispatcher reads what comes back, so the result is void. */
void fdps_chapter_24_event_deploy_wave_for_turn(int event_arg)
{
    /* The store the original makes over its own argument slot and never reads
       back; nothing here counts in it. */
    event_arg = 0;

    if (data_fdps_battle_turn_counter == CH24_WAVE_2_TURN) {
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH24_WAVE_2_TEXT_ID,
                       (unsigned char *) CH24_TURN_TEXT_DEST,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id, CH24_WAVE_2,
                         CH24_TURN_PLACE_ON_NEAREST_FREE_TILE);
    } else if (data_fdps_battle_turn_counter == CH24_LINE_ONLY_TURN) {
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH24_LINE_ONLY_TEXT_ID,
                       (unsigned char *) CH24_TURN_TEXT_DEST,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
    } else if (data_fdps_battle_turn_counter == CH24_SHAN_ARRIVAL_TURN) {
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id, CH24_SHAN_WAVE,
                         CH24_TURN_PLACE_ON_NEAREST_FREE_TILE);
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH24_SHAN_ARRIVAL_TEXT_ID,
                       (unsigned char *) CH24_TURN_TEXT_DEST,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
    } else if (data_fdps_battle_turn_counter == CH24_WAVE_4_TURN) {
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id, CH24_WAVE_4,
                         CH24_TURN_PLACE_ON_NEAREST_FREE_TILE);
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH24_WAVE_4_TEXT_ID,
                       (unsigned char *) CH24_TURN_TEXT_DEST,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
    } else if (data_fdps_battle_turn_counter == CH24_WAVE_3_TURN) {
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH24_WAVE_3_TEXT_ID,
                       (unsigned char *) CH24_TURN_TEXT_DEST,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id, CH24_WAVE_3,
                         CH24_TURN_PLACE_ON_NEAREST_FREE_TILE);
    } else if (data_fdps_battle_turn_counter == CH24_WAVE_5_TURN) {
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH24_WAVE_5_TEXT_ID,
                       (unsigned char *) CH24_TURN_TEXT_DEST,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id, CH24_WAVE_5,
                         CH24_TURN_PLACE_ON_NEAREST_FREE_TILE);
    }
}

/* The element of data_fdps_map_cell_event_triggered_flags (gamedata.h) the
   chapter 25 ambush latches: byte ptr [0x000640e8] at 00038e7b and 00038fc2,
   element 0x10 of the 32-byte array based at 0x000640d8.  The cell event codes
   a map file can carry only reach 0 to 15, so element 0x10 is the first slot no
   map cell can name and the chapter handlers keep their latch in it -- one slot
   shared by all of them, which is safe only because one chapter is loaded at a
   time and fdps_chapter_state_reset memsets the whole array when a chapter
   starts.

   Being inside that array is what keeps the latch honest.  The chapter reset
   clears all 0x20 bytes and the save and load paths move the whole array to and
   from the slot image, so a function-local static in its place would leave the
   ambush spent across a chapter restart and across a reload
   (rebuild_info/pitfalls.md).

   src/chevt1.c, src/chevt2.c, src/chevt4.c and src/chpost2.c spell the same
   slot out for the same reason; it stays file-local at every end because no
   header owns it. */
#define CHAPTER_EVENT_ONE_SHOT_SLOT 0x10

/* The side byte value the second gate tests for, MOV AL,byte ptr [EAX+0x6] /
   AND EAX,0xff / CMP EAX,0x2 at 00038e87.  2 is the player's own roster, 0 the
   enemy and 1 the guest, so an enemy walking over the trigger tile cannot
   spring the ambush.  The load widens the byte with AND EAX,0xff and not with
   a sign extension, so this is an unsigned equality on the whole byte.
   src/chevt4.c, src/chevt6.c, src/combat.c, src/deploy.c and src/unitatk.c
   spell the same constant out; it stays file-local at every end because no
   header owns it. */
#define PLAYER_SIDE 2

/* The half of the AI byte the merge below keeps: AND DL,0xf0 at 00038fb2.  The
   four bits it preserves are flags other code reads on their own -- 0x40 in
   fdps_map_actor_take_best_action and 0x80 in fdps_score_targets_for_item --
   while the four it drops are the behaviour code fdps_map_actor_behavior_step
   isolates with AND AL,0xf and dispatches on.  src/unit.c, src/chevt1.c,
   src/chevt2.c, src/chevt3.c, src/chevt4.c and src/chevt6.c spell the same mask
   out for the same field; it stays file-local at every end because no header
   owns it. */
#define AI_BEHAVIOR_FLAG_NIBBLE 0xf0

/* Where the chapter 25 ambush draws its two lines, PUSH 0xa0000 at 00038ea7 and
   00038f4b: the top-left corner of the visible mode-13h page, kept a literal
   because it is an address inside the display adapter's aperture rather than
   the address of anything the linker places (rebuild_info/pitfalls.md,
   contract E). */
#define CH25_TEXT_DEST 0x000a0000

/* The two entries of the chapter's own FDETXT25.TXT block the ambush speaks,
   PUSH 0x12 at 00038eac before the deployment and PUSH 0x13 at 00038f50 after
   the pan. */
#define CH25_AMBUSH_OPENING_TEXT_ID 0x12
#define CH25_AMBUSH_CLOSING_TEXT_ID 0x13

/* The wave the ambush brings on, PUSH 0x1 at 00038ebf, matched against byte
   0x15 of each 0x1a-byte deployment record of the resident MAP%02d.DAT block,
   and how it is placed: XOR EAX,EAX / PUSH EAX at 00038ebc, so each arrival
   settles on the nearest free walkable tile to its record's coordinates rather
   than on those coordinates themselves. */
#define CH25_AMBUSH_WAVE 1
#define CH25_AMBUSH_PLACE_ON_NEAREST_FREE_TILE 0

/* The two values written to data_fdps_map_cursor_draw_mode around the pan: 0 at
   00038ecf, which is outside the 1..6 range fdps_draw_map_cursor dispatches on
   so no cursor is painted over the sweep, and 1 at 00038f33, the plain cursor
   tile.  The value found in the global is NOT saved and restored -- 1 is a
   literal store -- so a mode other than 1 does not survive the ambush. */
#define CH25_MAP_CURSOR_BLANK 0
#define CH25_MAP_CURSOR_NORMAL 1

/* The two world pixels the pan walks to, PUSH 0x0 / PUSH 0x0 at 00038edb and
   PUSH 0x108 / PUSH 0x1e0 at 00038f03: tile (0, 0) in the top-left corner of
   the map and tile (20, 11) on the right-hand side of it, the two corners the
   wave lands in.  Both are whole multiples of the 24-pixel tile, which is what
   keeps fdps_map_cursor_move_to's step count off zero. */
#define CH25_PAN_NORTHWEST_X 0
#define CH25_PAN_NORTHWEST_Y 0
#define CH25_PAN_SOUTHEAST_X 0x1e0
#define CH25_PAN_SOUTHEAST_Y 0x108

/* How long each of the two holds lasts, CMP dword ptr [EBP-0x4],0xc / JL at
   00038eec and 00038f1c.  The test is a strict less-than against 12 on a
   counter seeded with 0, so twelve frames are composed at each stop. */
#define CH25_PAN_HOLD_FRAMES 0xc

/* The range the behaviour sweep covers and the mode it writes.  Twelve is the
   constant parked at [EBP-0x28] at 00038f60 and is the number of party slots
   MAP24.DAT reserves, so the sweep starts at the first unit that is not a party
   member; 0x0b at [EBP-0x20] at 00038f70 is the fullest of the map-AI chains,
   the one fdps_map_actor_behavior_step runs the enemy spell chooser ahead of
   the attack scorers for. */
#define CH25_ALERT_FIRST_UNIT_INDEX 0xc
#define CH25_ALERT_BEHAVIOR_MODE 0x0b

/* 00038e60.  Chapter 25's ambush: the first time a unit on the player's side
   finishes a step onto the map's trigger tile, the wave-1 reinforcements arrive
   in two corners of the map, the view is panned over both of them, and every
   unit on the map that is not a party slot switches to the fullest map-AI mode
   so the whole garrison attacks at once.

   The frame is the family's four-push one with 0x28 bytes of locals -- PUSH EBX
   / PUSH ESI / PUSH EDI / PUSH EBP / MOV EBP,ESP / SUB ESP,0x28 at
   00038e60..00038e66 -- so the one incoming dword sits at [EBP+0x14].  Every
   caller-clean in the body is this function's own (ADD ESP,0x1c after each
   draw, ADD ESP,0xc after the deployment, ADD ESP,0x8 after each pan and ADD
   ESP,0x4 after each record fetch), the RET at 00038fcf carries no immediate,
   and the dispatcher fdps_battle_run_turn_events pushes one dword and drops it
   with ADD ESP,0x4, so the convention is the stack one at both ends of the
   call.

   THE RECORD IS FETCHED BEFORE EITHER GATE.  The CALL at 00038e70 runs first
   and its result is parked at [EBP-0x8]; only then does CMP byte ptr
   [0x000640e8],0x0 / JNZ at 00038e7b test the latch.  fdps_get_unit_record is a
   base-plus-index computation with no side effect, so the fetch is invisible,
   but moving it inside the gate would be a different function.

   BOTH GATES LEAVE BY THE SAME EXIT.  The JNZ at 00038e82 and the JZ at
   00038e92 both reach the JMP at 00038e94 to the epilogue, so a latch that is
   already up and a unit on the wrong side are refused identically: nothing is
   drawn, nothing is deployed, the cursor mode is untouched and the latch is not
   spent.  An enemy that walks over the tile leaves the ambush armed for the
   unit that comes next.

   THE ORDER OF THE DEPLOYMENT AND THE SWEEP IS BEHAVIOUR.  MOV EAX,[0x00060150]
   / DEC EAX at 00038f67 reads the unit count AFTER fdps_deploy_wave has already
   appended the wave, so the arrivals are swept into behaviour mode 0x0b along
   with the garrison.  Reading the count before the deployment, or moving the
   deployment after the sweep, leaves the arriving wave in whatever mode the map
   file authored for it (rebuild_info/pitfalls.md).

   THE LOOP BOUND IS READ ONCE.  The count less one is computed into [EBP-0x24],
   copied to [EBP-0x18] and compared against from there every iteration; nothing
   in the loop re-reads the global.  Writing the bound back into the loop
   condition would re-read data_fdps_map_unit_count on each pass.

   THE SWEEP IS INCLUSIVE AND SIGNED.  MOV EAX,[EBP-0x10] / CMP EAX,[EBP-0x18] /
   JLE at 00038f8f, so the last index written is the count less one and not one
   past the end, and a map with fewer than thirteen units runs the loop zero
   times rather than wrapping.

   THE SWEEP IS AN INLINE EXPANSION AND NOT A CALL.  The three constants are
   parked at [EBP-0x28], [EBP-0x24] and [EBP-0x20] and copied into a second set
   of slots at [EBP-0x14], [EBP-0x18] and [EBP-0x1c] before the counter is
   seeded -- the fingerprint of fdps_object_set_field34_low_nibble_range
   expanded in place -- and the only CALL in the loop is fdps_get_unit_record,
   once per iteration.  Writing the range as a call to that helper would put a
   CALL in the rebuild that the original does not make.

   THE CURSOR MODE IS NOT SAVED AND RESTORED.  MOV dword ptr [0x00069cd0],0x1 at
   00038f33 is a literal store, so whatever the global held on entry is lost.

   NO VALUE IS USED AFTER A CALL EXCEPT THE TWO RECORD POINTERS.  Both
   fdps_draw_text cursors are discarded -- the next instruction after each CALL
   is the ADD ESP that cleans its arguments -- and fdps_deploy_wave,
   fdps_map_cursor_move_to and fdps_render_view_frame all return nothing.  The
   two fdps_get_unit_record results come back in EAX and are stored to [EBP-0x8]
   and [EBP-0xc]; the loop's copy is re-read once for the load of the AI byte
   and again for the store, so both halves of the merge address the record
   fetched by that iteration.  Nothing sets EAX before the RET and no dispatcher
   reads what comes back, so the result is void. */
void fdps_chapter_25_event_deploy_wave_1(int unit_index)
{
    /* The unit that tripped the tile event, resolved before either gate; only
       its side byte is read. */
    struct fdps_unit_record *triggering_unit;
    /* The record the sweep is writing the behaviour mode into. */
    struct fdps_unit_record *alerted_unit;
    /* The last index the sweep covers, taken from the unit count after the
       wave has landed and never refreshed inside the loop. */
    int last_unit_index;
    int alerted_unit_index;
    /* Which of the twelve frames a stop is holding for. */
    int hold_frame;

    triggering_unit = fdps_get_unit_record(unit_index);

    if (data_fdps_map_cell_event_triggered_flags[CHAPTER_EVENT_ONE_SHOT_SLOT]
            == 0
        && triggering_unit->side == PLAYER_SIDE) {
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH25_AMBUSH_OPENING_TEXT_ID,
                       (unsigned char *) CH25_TEXT_DEST, VGA_SCREEN_PITCH,
                       MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);

        fdps_deploy_wave(data_fdps_chapter_current_chapter_id, CH25_AMBUSH_WAVE,
                         CH25_AMBUSH_PLACE_ON_NEAREST_FREE_TILE);

        data_fdps_map_cursor_draw_mode = CH25_MAP_CURSOR_BLANK;

        fdps_map_cursor_move_to(CH25_PAN_NORTHWEST_X, CH25_PAN_NORTHWEST_Y);
        for (hold_frame = 0;
             hold_frame < CH25_PAN_HOLD_FRAMES;
             hold_frame++) {
            fdps_render_view_frame();
        }

        fdps_map_cursor_move_to(CH25_PAN_SOUTHEAST_X, CH25_PAN_SOUTHEAST_Y);
        for (hold_frame = 0;
             hold_frame < CH25_PAN_HOLD_FRAMES;
             hold_frame++) {
            fdps_render_view_frame();
        }

        data_fdps_map_cursor_draw_mode = CH25_MAP_CURSOR_NORMAL;

        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH25_AMBUSH_CLOSING_TEXT_ID,
                       (unsigned char *) CH25_TEXT_DEST, VGA_SCREEN_PITCH,
                       MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);

        last_unit_index = data_fdps_map_unit_count - 1;
        for (alerted_unit_index = CH25_ALERT_FIRST_UNIT_INDEX;
             alerted_unit_index <= last_unit_index;
             alerted_unit_index++) {
            alerted_unit = fdps_get_unit_record(alerted_unit_index);
            alerted_unit->ai_behavior = (unsigned char)
                ((alerted_unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
                 CH25_ALERT_BEHAVIOR_MODE);
        }

        data_fdps_map_cell_event_triggered_flags[
            CHAPTER_EVENT_ONE_SHOT_SLOT] = 1;
    }
}

/* The one unit index the sword upgrade answers to, CMP dword ptr [EBP+0x14],0x0
   / JNZ at 00038ff0.  Battle unit 0 is Randis: the deployment lays the player's
   roster down first and he is always its first record, so no chapter can
   present him at another index.  It is the same index the default defeat test
   fdps_unit_is_retired(0) watches. */
#define CH25_RANDIS_UNIT_INDEX 0

/* What fdps_unit_find_item_slot (unititem.h) reports when the item is not in
   the bag, CMP dword ptr [EBP-0x4],-0x1 / JNZ at 00038ff6. */
#define CH25_SWORD_NOT_CARRIED (-1)

/* The sword that is taken and the sword that is given, PUSH 0xa1 at 00038fdc
   and PUSH 0xa2 at 00039031: 火光之劍, which the chapter 20 event
   fdps_chapter_20_event_upgrade_randis_sword (chevt4.h) hands out in exchange
   for 灼烈之劍, and 真炎龍劍, the last link of that chain (assets/items.md). */
#define CH25_FLAME_SWORD_ITEM_ID 0xa1
#define CH25_TRUE_DRAGON_SWORD_ITEM_ID 0xa2

/* The entry of the chapter's own FDETXT25.TXT block the fire god's line is
   spoken from, PUSH 0x14 at 00039011.  It is the entry after the two the
   chapter's ambush speaks. */
#define CH25_UPGRADE_TEXT_ID 0x14

/* 00038fd0.  Chapter 25's fire-god exchange: Randis finishes a step onto the
   shrine tile still carrying 火光之劍 and it becomes 真炎龍劍.

   The frame is the family's four-push one with a single 4-byte local -- PUSH
   EBX / PUSH ESI / PUSH EDI / PUSH EBP / MOV EBP,ESP / SUB ESP,0x4 at
   00038fd0..00038fd6 -- and that local is the slot number at [EBP-0x4], so the
   one incoming dword sits at [EBP+0x14].  Every caller-clean in the body is
   this function's own (ADD ESP,0x8 after the slot search, ADD ESP,0x1c after
   the draw, ADD ESP,0x8 after each of the two inventory calls and ADD ESP,0x4
   after the stat rebuild), and the RET at 00039054 carries no immediate, so the
   convention is the stack one at both ends of the call.

   THE SEARCH RUNS BEFORE BOTH GATES.  fdps_unit_find_item_slot is called at
   00038fe5, unconditionally, and only then are the two tests made at
   00038ff0..00038ffa: the unit index first, the search result second.  So every
   firing -- including a firing by a unit that is not Randis -- costs one walk
   of that unit's inventory.  Folding the search into the condition, which is
   what the short-circuit spelling of the same test would do, moves the call
   inside the gates and skips it on those firings.

   THERE IS NO TURN DEADLINE AND NO ONE-SHOT LATCH.  The chapter 20 link of the
   same chain closes at turn 20; this one has no turn test at all, and nothing
   in the body writes data_fdps_map_cell_event_triggered_flags.  What stops the
   exchange happening twice is the item itself: the search misses once the sword
   has become 真炎龍劍.  That is also why a unit which is not Randis may walk
   over the tile without spending the event.

   THE REMOVAL IS AHEAD OF THE ADDITION.  fdps_unit_remove_item at 00039029 and
   only then fdps_unit_add_item at 0003903a.  fdps_unit_add_item fills the first
   empty entry of an eight-entry inventory and stores nothing at all when the
   eight are full, so with the old sword still in the bag a full-handed Randis
   would be given nothing and then have 火光之劍 taken off him.

   ALL THREE CALLS AFTER THE DRAW TAKE THE PARAMETER.  MOV EAX,dword ptr
   [EBP+0x14] at 00039025, 00039036 and 00039042 -- unlike the chapter 20 link,
   whose addition is handed a literal 0.  The gate above has already forced the
   parameter to be 0, so the two spellings behave alike, and each is kept where
   its own assembly has it.

   Only one value is used after a CALL: fdps_unit_find_item_slot's, which comes
   back in EAX and is stored to [EBP-0x4] at 00038fed, then reloaded at 00038ff6
   for the gate and at 00039021 for the removal.  fdps_draw_text's cursor is
   discarded -- the next instruction is the ADD ESP,0x1c that cleans its
   arguments -- fdps_unit_add_item's 1-or--1 result is discarded likewise, and
   fdps_unit_recompute_combat_stats returns nothing.  Nothing sets EAX before
   the RET and no dispatcher reads what comes back, so the result is void. */
void fdps_chapter_25_event_upgrade_randis_sword(int unit_index)
{
    /* Which of the triggering unit's inventory entries holds 火光之劍, or -1;
       searched before either gate and reused as the entry to empty. */
    int sword_slot;

    sword_slot = fdps_unit_find_item_slot(unit_index,
                                          CH25_FLAME_SWORD_ITEM_ID);

    if (unit_index == CH25_RANDIS_UNIT_INDEX
            && sword_slot != CH25_SWORD_NOT_CARRIED) {
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH25_UPGRADE_TEXT_ID,
                       (unsigned char *) CH25_TEXT_DEST, VGA_SCREEN_PITCH,
                       MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        fdps_unit_remove_item(unit_index, sword_slot);
        fdps_unit_add_item(unit_index, CH25_TRUE_DRAGON_SWORD_ITEM_ID);
        fdps_unit_recompute_combat_stats(unit_index);
    }
}

/* The character the shop answers to, MOV AL,byte ptr [EAX+0x8] / AND EAX,0xff /
   CMP EAX,0x5 at 0003907e: the id byte fdps_roster_add_character stamps into
   record offset 8 when the character joins, and 5 is 瑪麗安 the 弓兵
   (assets/characters.md).  It is the same byte
   fdps_battle_find_unit_by_character_id (unit.h) matches its argument against.
   The load widens the byte with AND EAX,0xff and not with a sign extension, so
   this is an unsigned equality on the whole byte. */
#define CH25_MARIAN_CHARACTER_ID 5

/* What the bow costs and what is handed over: CMP dword ptr [0x000643a4],0x7530
   at 0003908b and SUB dword ptr [0x000643a4],0x7530 at 0003914e, and PUSH 0x4a
   at 00039140.  Item 0x4a is 風神弓 and 30000 is exactly its list price in
   assets/items.md, so the merchant asks the shop rate rather than a mark-up.

   THE PURSE TEST IS SIGNED, JGE at 00039095 and not JAE, so the amount is read
   as an int and a purse that had somehow gone negative fails the gate rather
   than passing it as a huge unsigned figure (contract C). */
#define CH25_WIND_GOD_BOW_PRICE 0x7530
#define CH25_WIND_GOD_BOW_ITEM_ID 0x4a

/* The element of data_fdps_map_cell_event_triggered_flags this offer latches,
   byte ptr [0x000640e9] -- element 0x11 of the array based at 0x000640d8, and
   NOT the element 0x10 the chapter 25 ambush above uses.  It is shared: five
   other handlers latch the same byte for their own one-shot events
   (fdps_chapter_03_event_deploy_wave_14, fdps_chapter_08_event_villager_escapes,
   fdps_chapter_21_event_deploy_wave_2, fdps_chapter_19_post_action and
   fdps_chapter_24_post_action), each of them in a chapter this one cannot share
   a map with.  fdps_chapter_state_reset clears all 0x20 bytes and the save and
   load paths move the whole array to and from the slot image, so a
   function-local static in its place would leave the offer spent across a
   chapter restart and across a reload (rebuild_info/pitfalls.md). */
#define CH25_BOW_OFFER_ONE_SHOT_SLOT 0x11

/* The count that means there is no room, CMP EAX,0x8 / JNZ at 000390b0.  A unit
   record holds eight inventory entries and fdps_unit_item_count answers how
   many of them are occupied (unititem.h), so eight is a full bag.  The test is
   an inequality against 8 and not a "< 8", which matters only if the count
   could ever exceed eight; it cannot, so the two spellings agree, and this one
   is the instruction's. */
#define CH25_INVENTORY_FULL 8

/* The two destinations the four draws are handed.  0xa0000 at 000390c8 is the
   top-left corner of the visible mode-13h page and 0xaa44a at 000390f5,
   0003912b and 00039168 is screen (138, 131), the pen the message panel's own
   text sits at.  Both stay literals because they are addresses inside the
   display adapter's aperture rather than the addresses of anything the linker
   places (rebuild_info/pitfalls.md, contract E). */
#define CH25_BOW_TEXT_DEST 0x000a0000
#define CH25_BOW_PANEL_TEXT_ORIGIN 0x000aa44a

/* The four entries of the chapter's own FDETXT25.TXT block this event speaks,
   PUSH 0x15 at 000390cd, PUSH 0x16 at 000390fa, PUSH 0x17 at 00039130 and PUSH
   0x18 at 0003916d: the merchant's pitch, his question, the sale and the
   refusal.  They are the four entries after the ones the sword upgrade and the
   ambush above use. */
#define CH25_BOW_OFFER_TEXT_ID 0x15
#define CH25_BOW_QUESTION_TEXT_ID 0x16
#define CH25_BOW_BOUGHT_TEXT_ID 0x17
#define CH25_BOW_DECLINED_TEXT_ID 0x18

/* The portrait the question's panel is opened with, PUSH 0x77 at 000390dd:
   FACE.CEL record 0x77, the merchant. */
#define CH25_MERCHANT_FACE 0x77

/* The prompt answer that buys, CMP dword ptr [EBP-0x4],0x0 / JNZ at 00039117.
   fdps_prompt_two_choice (msgwin.h) answers 0 for the left cell, 1 for the
   right and -1 for a cancel, and only 0 is tested for, so the cancel declines
   along with the right cell. */
#define CH25_BOW_ANSWER_BUY 0

/* 00039060.  Chapter 25's 風神弓 shop: 瑪麗安 stops on the merchant's tile
   with the money and the room for the bow, and is offered it once.

   The frame is the family's four-push one with two 4-byte locals -- PUSH EBX /
   PUSH ESI / PUSH EDI / PUSH EBP / MOV EBP,ESP / SUB ESP,0x8 at
   00039060..00039066 -- so the one incoming dword sits at [EBP+0x14].  Every
   caller-clean in the body is this function's own (ADD ESP,0x4 after each of
   the three unit calls, ADD ESP,0x1c after each of the four draws) and the RET
   at 0003918a carries no immediate, so the convention is the stack one at this
   end.  It is the stack one at the other end too: the seven dispatchers reach
   this slot as PUSH EAX / CALL dword ptr [EDX + 0x601c4] / ADD ESP,0x4 -- at
   00015839 in fdps_battle_unit_turn and at 000188d8 in
   fdps_battle_search_cell_at_cursor, among others -- pushing the unit index and
   cleaning the one argument themselves.

   THE FOUR GATES ARE A SHORT-CIRCUIT CHAIN AND THEIR ORDER IS THE ASSEMBLY'S.
   The character id at 00039086, the purse at 0003908b, the latch at 00039099
   and only then fdps_unit_item_count at 000390a8 -- the count is the one gate
   that costs a call, and the three cheap tests are ahead of it, so a firing by
   any unit that is not 瑪麗安 never walks an inventory.  Every failing gate
   reaches the jump at 000390b5 and leaves the latch clear.

   THE ITEM IS ADDED WITHOUT THE COUNT BEING ASKED AGAIN.  The room test at
   000390b0 is made before the question is put, and the modal prompt in between
   cannot change the bag, so fdps_unit_add_item at 00039146 always has an entry
   to fill and its 1-or--1 answer is discarded.

   THE LATCH IS RAISED ON BOTH ARMS.  MOV byte ptr [0x000640e9],0x1 at 0003917d
   sits below the join at 00039158, so the decline path passes through it as
   well as the purchase path.

   Two values are used after a CALL.  fdps_get_unit_record's record pointer
   comes back in EAX and is stored to [EBP-0x8] at 00039078, then reloaded at
   0003907b for the character id load; nothing between the two calls can move
   the unit array.  fdps_prompt_two_choice's answer comes back in EAX and is
   stored to [EBP-0x4] at 0003910f BEFORE fdps_message_window_close is called at
   00039112, and it is the saved copy at [EBP-0x4] that 00039117 compares -- the
   close's own EAX never reaches the test.  Everything else is discarded: each
   of the four fdps_draw_text cursors is followed straight by the ADD ESP,0x1c
   that cleans its arguments, fdps_unit_item_count's count is compared in EAX
   and not kept, fdps_unit_add_item's result is dropped, and
   fdps_message_window_open and fdps_message_window_close return nothing.
   Nothing sets EAX before the RET and no dispatcher reads what comes back, so
   the result is void. */
void fdps_chapter_25_event_marian_buys_wind_god_bow(int unit_index)
{
    /* The unit that stopped on the merchant's tile, resolved before any gate;
       only its character id byte is read. */
    struct fdps_unit_record *triggering_unit;
    /* What the player answered the merchant: 0 buys, and both the right cell's
       1 and a cancel's -1 decline. */
    int purchase_answer;

    triggering_unit = fdps_get_unit_record(unit_index);

    if (triggering_unit->char_id == CH25_MARIAN_CHARACTER_ID
            && data_fdps_shared_party_total_gold >= CH25_WIND_GOD_BOW_PRICE
            && data_fdps_map_cell_event_triggered_flags[
                   CH25_BOW_OFFER_ONE_SHOT_SLOT] == 0
            && fdps_unit_item_count(unit_index) != CH25_INVENTORY_FULL) {
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH25_BOW_OFFER_TEXT_ID,
                       (unsigned char *) CH25_BOW_TEXT_DEST, VGA_SCREEN_PITCH,
                       MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);

        fdps_message_window_open(CH25_MERCHANT_FACE);
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH25_BOW_QUESTION_TEXT_ID,
                       (unsigned char *) CH25_BOW_PANEL_TEXT_ORIGIN,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        purchase_answer = fdps_prompt_two_choice();
        fdps_message_window_close();

        if (purchase_answer == CH25_BOW_ANSWER_BUY) {
            fdps_draw_text(data_fdps_current_chapter_text_ptr,
                           CH25_BOW_BOUGHT_TEXT_ID,
                           (unsigned char *) CH25_BOW_PANEL_TEXT_ORIGIN,
                           VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                           MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
            fdps_unit_add_item(unit_index, CH25_WIND_GOD_BOW_ITEM_ID);
            data_fdps_shared_party_total_gold -= CH25_WIND_GOD_BOW_PRICE;
        } else {
            fdps_draw_text(data_fdps_current_chapter_text_ptr,
                           CH25_BOW_DECLINED_TEXT_ID,
                           (unsigned char *) CH25_BOW_PANEL_TEXT_ORIGIN,
                           VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                           MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
        }

        data_fdps_map_cell_event_triggered_flags[
            CH25_BOW_OFFER_ONE_SHOT_SLOT] = 1;
    }
}

/* Where chapter 26's advance order draws its line, PUSH 0xa0000 at 000391b1:
   the top-left corner of the visible mode-13h page, kept a literal because it
   is an address inside the display adapter's aperture rather than the address
   of anything the linker places (rebuild_info/pitfalls.md, contract E). */
#define CH26_TEXT_DEST 0x000a0000

/* The entry of the chapter's own FDETXT26.TXT block the order speaks, PUSH
   0x16 at 000391b6.  The message's token stream opens with speaker code -0x11
   and operand 0x43, so the line is spoken by character id 0x43, one of the
   chapter's four commanders. */
#define CH26_ADVANCE_TEXT_ID 0x16

/* The range the behaviour sweep covers and the mode it writes: 0x1a parked at
   [EBP-0x20] at 000391c6, 0x2d at [EBP-0x1c] at 000391cd and 0 at [EBP-0x18]
   at 000391d4.  0x1a is the first of the twenty records MAP25.DAT deploys in
   the holding mode after the twelve party slots and the fourteen records below
   them, 0x2d is the last of them, and mode 0 is the default movement chain
   that paths a unit toward the nearest opposing unit. */
#define CH26_ADVANCE_FIRST_UNIT_INDEX 0x1a
#define CH26_ADVANCE_LAST_UNIT_INDEX 0x2d
#define CH26_ADVANCE_BEHAVIOR_MODE 0

/* 00039190.  Chapter 26's turn-4 advance order: a line of the chapter's text
   is painted straight onto the screen and then the tower garrison's two
   holding groups are switched to the advancing behaviour mode.

   The frame is the family's four-push one with 0x20 bytes of locals -- PUSH
   EBX / PUSH ESI / PUSH EDI / PUSH EBP / MOV EBP,ESP / SUB ESP,0x20 at
   00039190..00039196 -- so the one incoming dword sits at [EBP+0x14].  Both
   caller-cleans in the body are this function's own (ADD ESP,0x1c after the
   draw at 000391c3 and ADD ESP,0x4 after each record fetch at 0003920a), the
   RET at 0003922c carries no immediate, and fdps_battle_run_turn_events -- the
   dispatcher that reaches this table slot -- pushes one dword and drops it with
   ADD ESP,0x4, so the convention is the stack one at both ends of the call.

   THE INCOMING ARGUMENT IS NEVER READ.  MOV dword ptr [EBP+0x14],0x0 at
   0003919c is the only access to the slot in the whole body and nothing loads
   it afterwards.  The dispatcher pushes a literal 0 anyway, so no value is
   lost by the store.

   THE SWEEP IS INCLUSIVE AND SIGNED.  MOV EAX,[EBP-0x8] / CMP EAX,[EBP-0x10] /
   JLE at 000391f3..000391f9, so unit 0x2d is written and a rewrite with a
   strict less-than would leave it holding position.

   THE MERGE KEEPS THE HIGH NIBBLE.  MOV DL,byte ptr [EAX+0x34] / AND DL,0xf0 /
   MOV DH,byte ptr [EBP-0x14] / OR DH,DL / MOV byte ptr [EAX+0x34],DH at
   00039213..00039221 reads the byte back, drops only its low four bits and
   writes the merged value.  Writing the mode as a plain store also wipes bits
   0x40 and 0x80, which fdps_map_actor_take_best_action (00012c6f) and
   fdps_score_targets_for_item (0001337d) test as independent per-unit flags
   (rebuild_info/pitfalls.md).

   THE SWEEP IS AN INLINE EXPANSION AND NOT A CALL.  The three constants are
   parked at [EBP-0x20], [EBP-0x1c] and [EBP-0x18] and copied into a second set
   of slots at [EBP-0xc], [EBP-0x10] and [EBP-0x14] before the counter is
   seeded -- the fingerprint of fdps_object_set_field34_low_nibble_range
   expanded in place -- and the only CALL inside the loop is
   fdps_get_unit_record, once per iteration.  Writing the range as a call to
   that helper would put a CALL in the rebuild that the original does not make.

   ONE VALUE IS USED AFTER A CALL.  The fdps_draw_text cursor is discarded --
   the next instruction after the CALL at 000391be is the ADD ESP that cleans
   its arguments -- while fdps_get_unit_record's record pointer comes back in
   EAX and is stored to [EBP-0x4], which is re-read once for the load of the AI
   byte and again for the store, so both halves of the merge address the record
   that iteration fetched.  Nothing sets EAX before the RET and no dispatcher
   reads what comes back, so the result is void. */
void fdps_chapter_26_event_enemies_advance(int unit_index)
{
    /* The record the sweep is writing the behaviour mode into, re-resolved on
       every pass. */
    struct fdps_unit_record *advancing_unit;
    int advancing_unit_index;

    /* The store the original makes over its own argument slot and never reads
       back; nothing here counts in it. */
    unit_index = 0;

    fdps_draw_text(data_fdps_current_chapter_text_ptr, CH26_ADVANCE_TEXT_ID,
                   (unsigned char *) CH26_TEXT_DEST, VGA_SCREEN_PITCH,
                   MESSAGE_FG_COLOR, MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);

    for (advancing_unit_index = CH26_ADVANCE_FIRST_UNIT_INDEX;
         advancing_unit_index <= CH26_ADVANCE_LAST_UNIT_INDEX;
         advancing_unit_index++) {
        advancing_unit = fdps_get_unit_record(advancing_unit_index);
        advancing_unit->ai_behavior = (unsigned char)
            ((advancing_unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
             CH26_ADVANCE_BEHAVIOR_MODE);
    }
}

/* Where chapter 26's ambush draws its three lines, PUSH 0xa0000 at 0003928a,
   000392f1 and 00039327: the top-left corner of the visible mode-13h page,
   kept a literal because it is an address inside the display adapter's
   aperture rather than the address of anything the linker places
   (rebuild_info/pitfalls.md, contract E). */
#define CH26_WAVES_TEXT_DEST 0x000a0000

/* The three entries of the chapter's own FDETXT26.TXT block the ambush speaks,
   named for where they fall in the sequence rather than for what they say:
   PUSH 0xa at 0003928f is drawn once the enemy wave is on the board, PUSH 0xb
   at 000392f6 once the pan has come back, and PUSH 0xc at 0003932c once the
   allied wave has landed.  The shipped FDETXT26.TXT carries 25 entries, so all
   three are live; entry 10's token stream opens with speaker code -0x11 and
   operand 0x43 -- the same commander the turn-4 order above speaks through --
   entry 11's with operand 2 and entry 12's with operand 0x0c. */
#define CH26_WAVES_AFTER_ENEMY_WAVE_TEXT_ID 0x0a
#define CH26_WAVES_BEFORE_ALLY_WAVE_TEXT_ID 0x0b
#define CH26_WAVES_AFTER_ALLY_WAVE_TEXT_ID 0x0c

/* The two waves the ambush brings on, PUSH 0x2 at 0003926c and PUSH 0x3 at
   00039309, each matched against byte 0x15 of every 0x1a-byte deployment
   record of the resident MAP%02d.DAT block.  On MAP25.DAT wave 2 is a block of
   side-0 enemies and wave 3 a block of side-1 allies, which is why the two
   deployments are not interchangeable: the enemy wave lands before the pan and
   the allied one after it. */
#define CH26_WAVES_ENEMY_WAVE 2
#define CH26_WAVES_ALLY_WAVE 3

/* How every arrival is placed, XOR EAX,EAX / PUSH EAX at 00039269 and
   00039306: each settles on the nearest free walkable tile to its record's
   coordinates rather than on those coordinates themselves. */
#define CH26_WAVES_PLACE_ON_NEAREST_FREE_TILE 0

/* The two values written to data_fdps_map_cursor_draw_mode around the pan: 0
   at 0003929f, which is outside the 1..6 range fdps_draw_map_cursor dispatches
   on so no cursor is painted over the sweep, and 1 at 000392d9, the plain
   cursor tile.  The value found in the global is NOT saved and restored -- 1
   is a literal store -- so a mode other than 1 does not survive the ambush. */
#define CH26_WAVES_MAP_CURSOR_BLANK 0
#define CH26_WAVES_MAP_CURSOR_NORMAL 1

/* The world pixel the pan walks to, PUSH 0x378 at 000392a9 and PUSH 0xf0 at
   000392ae: tile (10, 37) at the 24 pixels a tile measures, the bottom of the
   map both waves arrive at.  Both coordinates are whole multiples of the tile,
   which is what keeps fdps_map_cursor_move_to's step count off zero. */
#define CH26_WAVES_PAN_X 0xf0
#define CH26_WAVES_PAN_Y 0x378

/* How long the hold at the far end of the pan lasts, CMP dword ptr
   [EBP+0x14],0xc / JL at 000392c2.  The test is a strict less-than against 12
   on a counter seeded with 0, so twelve frames are composed at the stop. */
#define CH26_WAVES_PAN_HOLD_FRAMES 0xc

/* The range the behaviour sweep covers and the mode it writes: 0xc parked at
   [EBP-0x24] at 0003933c, 0x4f at [EBP-0x20] at 00039343 and 0 at [EBP-0x1c]
   at 0003934a.  Twelve is the number of party slots a deployment lays down
   first, so the sweep starts at the first record that is not a party member.
   0x4f is a literal and is NOT the unit count -- which chapter 25's ambush
   next door does read -- so every unit from index 0x50 upward is left on
   whatever behaviour mode it already carries, the two waves this handler has
   just appended included.  Mode 0 is the default movement chain that paths a
   unit toward the nearest opposing unit.

   On MAP25.DAT, the only map that reaches this slot, the two literals cover
   the wave-0 block exactly: the file's record count byte is 0x50 and the 80
   records split 68 tagged wave 0, seven tagged wave 2 and five tagged wave 3,
   so the twelve party slots plus the 68 wave-0 arrivals fill indices 0x00
   through 0x4f and the twelve units this handler appends fill 0x50 through
   0x5b.  The sweep therefore releases the garrison and nothing else -- neither
   the enemy wave nor the allied wave it has just brought on. */
#define CH26_WAVES_FIRST_RELEASED_INDEX 0xc
#define CH26_WAVES_LAST_RELEASED_INDEX 0x4f
#define CH26_WAVES_BEHAVIOR_MODE 0

/* 00039230.  Chapter 26's mid-map ambush: the first time a unit on the
   player's side walks onto the map's trigger tile, the enemy wave comes on,
   the view is panned down to the bottom of the map and held there, the allied
   relief wave comes on behind it, and the whole lower half of the deployment
   block is switched out of holding position into the advancing behaviour mode.

   The frame is the family's four-push one with 0x24 bytes of locals -- PUSH
   EBX / PUSH ESI / PUSH EDI / PUSH EBP / MOV EBP,ESP / SUB ESP,0x24 at
   00039230..00039236 -- so the one incoming dword sits at [EBP+0x14].  Every
   caller-clean in the body is this function's own (ADD ESP,0xc after each
   deployment, ADD ESP,0x1c after each draw, ADD ESP,0x8 after the pan and ADD
   ESP,0x4 after each record fetch), the RET at 000393a9 carries no immediate,
   and the dispatcher that reaches this table slot pushes one dword and drops
   it with ADD ESP,0x4 -- MOV EAX,[EBP+0x14] / PUSH EAX / CALL dword ptr
   [EDX + 0x601c4] / ADD ESP,0x4 at 00015835..0001583f -- so the convention is
   the stack one at both ends of the call.

   THE RECORD IS FETCHED BEFORE EITHER GATE.  The CALL at 00039240 runs first
   and its result is parked at [EBP-0x4]; only then does CMP byte ptr
   [0x000640e8],0x0 / JNZ at 0003924b test the latch.  fdps_get_unit_record is
   a base-plus-index computation with no side effect, so the fetch is
   invisible, but moving it inside the gate would be a different function.

   BOTH GATES LEAVE BY THE SAME EXIT.  The JNZ at 00039252 and the JZ at
   00039262 both reach the JMP at 00039264 to the epilogue, so a latch that is
   already up and a unit on the wrong side are refused identically: nothing is
   deployed, nothing is drawn, the cursor mode is untouched and the latch is
   not spent.  An enemy or a guest that walks over the tile leaves the ambush
   armed for the unit that comes next.

   THE LATCH IS THE ONE CHAPTER 25's AMBUSH USES.  Both handlers test and set
   element 0x10 of the same block.  They cannot collide because the two events
   live on different maps and the block is cleared between chapters.

   THE ENEMY WAVE LANDS BEFORE ITS LINE AND THE ALLIED ONE AFTER ITS LINE.  The
   deployment at 00039274 comes ahead of the draw at 00039297, while the draw
   at 000392fe comes ahead of the deployment at 00039311 and a third draw at
   00039334 follows it.  What the player sees is therefore an enemy wave that
   is already standing when it is announced and an allied wave that is
   announced before it arrives; reordering either pair is visible.

   THE SWEEP IS INCLUSIVE AND SIGNED.  MOV EAX,[EBP-0xc] / CMP EAX,[EBP-0x14] /
   JLE at 00039369..0003936f, so unit 0x4f is written and a rewrite with a
   strict less-than would leave it holding position.

   THE MERGE KEEPS THE HIGH NIBBLE.  MOV DL,byte ptr [EAX+0x34] / AND DL,0xf0 /
   MOV DH,byte ptr [EBP-0x18] / OR DH,DL / MOV byte ptr [EAX+0x34],DH at
   00039389..00039397 reads the byte back, drops only its low four bits and
   writes the merged value.  The OR operand is 0 here, so the whole store looks
   like a plain zeroing; writing it as one also wipes bits 0x40 and 0x80, which
   fdps_map_actor_take_best_action (00012c6f) and fdps_score_targets_for_item
   (0001337d) test as independent per-unit flags (rebuild_info/pitfalls.md).

   THE SWEEP IS AN INLINE EXPANSION AND NOT A CALL.  The three constants are
   parked at [EBP-0x24], [EBP-0x20] and [EBP-0x1c] and copied into a second set
   of slots at [EBP-0x10], [EBP-0x14] and [EBP-0x18] before the counter is
   seeded -- the fingerprint of fdps_object_set_field34_low_nibble_range
   expanded in place -- and the only CALL inside the loop is
   fdps_get_unit_record, once per iteration.  Writing the range as a call to
   that helper would put a CALL in the rebuild that the original does not make.

   THE SWEEP IS NOT BOUNDED BY THE UNIT COUNT.  Both ends are literals, and on
   MAP25.DAT they land on the wave-0 block exactly, so the loop never reaches
   past a live record there; on a map carrying fewer records it would.  Adding
   a bound would be a different function.

   THE HOLD REUSES THE ARGUMENT SLOT.  MOV dword ptr [EBP+0x14],0x0 at
   000392bb seeds the twelve-frame counter over the incoming index, which by
   then has already been spent on the record fetch and is never read again.
   The counter is emitted as a local of its own here; nothing observes the
   difference.

   THE CURSOR MODE IS NOT SAVED AND RESTORED.  MOV dword ptr [0x00069cd0],0x1
   at 000392d9 is a literal store, so whatever the global held on entry is
   lost.

   NO VALUE IS USED AFTER A CALL EXCEPT THE TWO RECORD POINTERS.  All three
   fdps_draw_text cursors are discarded -- the next instruction after each CALL
   is the ADD ESP that cleans its arguments -- and fdps_deploy_wave,
   fdps_map_cursor_move_to and fdps_render_view_frame all return nothing.  The
   two fdps_get_unit_record results come back in EAX and are stored to [EBP-0x4]
   and [EBP-0x8]; the loop's copy is re-read once for the load of the AI byte
   and again for the store, so both halves of the merge address the record that
   iteration fetched.  Nothing sets EAX before the RET and no dispatcher reads
   what comes back, so the result is void. */
void fdps_chapter_26_event_deploy_waves_2_and_3(int unit_index)
{
    /* The unit that tripped the tile event, resolved before either gate; only
       its side byte is read. */
    struct fdps_unit_record *triggering_unit;
    /* The record the sweep is writing the behaviour mode into, re-resolved on
       every pass. */
    struct fdps_unit_record *released_unit;
    int released_unit_index;
    /* Which of the twelve frames the stop at the far end of the pan is
       holding for. */
    int hold_frame;

    triggering_unit = fdps_get_unit_record(unit_index);

    if (data_fdps_map_cell_event_triggered_flags[CHAPTER_EVENT_ONE_SHOT_SLOT]
            == 0
        && triggering_unit->side == PLAYER_SIDE) {
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id,
                         CH26_WAVES_ENEMY_WAVE,
                         CH26_WAVES_PLACE_ON_NEAREST_FREE_TILE);

        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH26_WAVES_AFTER_ENEMY_WAVE_TEXT_ID,
                       (unsigned char *) CH26_WAVES_TEXT_DEST,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);

        data_fdps_map_cursor_draw_mode = CH26_WAVES_MAP_CURSOR_BLANK;

        fdps_map_cursor_move_to(CH26_WAVES_PAN_X, CH26_WAVES_PAN_Y);
        for (hold_frame = 0;
             hold_frame < CH26_WAVES_PAN_HOLD_FRAMES;
             hold_frame++) {
            fdps_render_view_frame();
        }

        data_fdps_map_cursor_draw_mode = CH26_WAVES_MAP_CURSOR_NORMAL;

        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH26_WAVES_BEFORE_ALLY_WAVE_TEXT_ID,
                       (unsigned char *) CH26_WAVES_TEXT_DEST,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);

        fdps_deploy_wave(data_fdps_chapter_current_chapter_id,
                         CH26_WAVES_ALLY_WAVE,
                         CH26_WAVES_PLACE_ON_NEAREST_FREE_TILE);

        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH26_WAVES_AFTER_ALLY_WAVE_TEXT_ID,
                       (unsigned char *) CH26_WAVES_TEXT_DEST,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);

        for (released_unit_index = CH26_WAVES_FIRST_RELEASED_INDEX;
             released_unit_index <= CH26_WAVES_LAST_RELEASED_INDEX;
             released_unit_index++) {
            released_unit = fdps_get_unit_record(released_unit_index);
            released_unit->ai_behavior = (unsigned char)
                ((released_unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
                 CH26_WAVES_BEHAVIOR_MODE);
        }

        data_fdps_map_cell_event_triggered_flags[
            CHAPTER_EVENT_ONE_SHOT_SLOT] = 1;
    }
}

/* The element of data_fdps_map_cell_event_triggered_flags the wipe line
   latches, byte ptr [0x000640ea] at 000393c3 and 00039428 -- element 0x12 of
   the array based at 0x000640d8, and NOT element 0x10, which the two ambushes
   above share.  A sweep of the whole image for that address finds four
   instructions: these two and the pair in fdps_chapter_27_event_deploy_wave_1
   at 00039453 and 00039533, which latches the same element for its own one-shot
   event one chapter later.  The two cannot collide because one chapter is
   loaded at a time and fdps_chapter_state_reset memsets the whole 0x20-byte
   block when a chapter starts; that memset, and the save and load paths that
   move the whole block to and from the slot image, are also why the latch has
   to live in the array and not in a function-local static, which would leave
   the line spoken across a chapter restart and across a reload
   (rebuild_info/pitfalls.md). */
#define CH26_WIPE_ONE_SHOT_SLOT 0x12

/* The seven unit records the wipe watches: ADD EAX,0x50 at 000393e6 over a
   counter the CMP dword ptr [EBP+0x14],0x7 / JL at 000393d3 runs from 0 to 6,
   so indices 0x50 through 0x56.  On MAP25.DAT those are exactly the seven
   wave-2 records fdps_chapter_26_event_deploy_waves_2_and_3 above appends: the
   twelve party slots occupy 0x00..0x0b, the map's 68 wave-0 records 0x0c..0x4f,
   and the five wave-3 allies land at 0x57..0x5b behind them.  Both numbers are
   literals and neither is derived from data_fdps_map_unit_count. */
#define CH26_WAVE_2_FIRST_UNIT_INDEX 0x50
#define CH26_WAVE_2_UNIT_COUNT 7

/* The entry of the chapter's own FDETXT26.TXT block the line is spoken from,
   PUSH 0x17 at 00039418.  Its token stream opens with speaker code -0x11 and
   operand 0x0c, MAP25.DAT's single wave-3 ally, so the line belongs to the
   guest hero the allied wave brings on and not to any of the seven it is spoken
   over. */
#define CH26_WIPE_TEXT_ID 0x17

/* Where the line is drawn, PUSH 0xa0000 at 00039413: the top-left corner of the
   visible mode-13h page, kept a literal because it is an address inside the
   display adapter's aperture rather than the address of anything the linker
   places (rebuild_info/pitfalls.md, contract E). */
#define CH26_WIPE_TEXT_DEST 0x000a0000

/* 000393b0.  Chapter 26's wave-2 wipe line: the death script all seven of the
   chapter's wave-2 reinforcements carry, which speaks one line once the last of
   them has gone and then latches itself off.

   The frame is the family's four-push one with a single 4-byte local -- PUSH
   EBX / PUSH ESI / PUSH EDI / PUSH EBP / MOV EBP,ESP / SUB ESP,0x4 at
   000393b0..000393b6 -- and that local at [EBP-0x4] is the survivor flag, so
   the one incoming dword sits at [EBP+0x14].  Both caller-cleans in the body
   are this function's own (ADD ESP,0x4 after each retirement test at 000393ef
   and ADD ESP,0x1c after the draw at 00039425) and the RET at 00039435 carries
   no immediate, so the convention is the stack one at this end.  It is the
   stack one at the other end too: the dispatcher fdps_run_death_scripts reaches
   this table slot as MOV EAX,[EBP+0x14] / PUSH EAX / CALL dword ptr
   [EDX + 0x601c4] / ADD ESP,0x4 at 0001dcaa..0001dcb4, pushing one dword and
   cleaning it itself.

   BOTH GATES LEAVE BY THE SAME EXIT.  The JNZ at 000393ca on the latch and the
   JNZ at 00039403 on the survivor flag both go to the epilogue at 0003942f, so
   a firing that finds the line already spoken and a firing that finds one of
   the seven still standing are refused identically: nothing is drawn and the
   latch is not touched.

   THE LATCH TEST IS "NOT ZERO" AND NOT "NOT ONE".  CMP byte ptr
   [0x000640ea],0x0 at 000393c3, so any non-zero value in the element blocks the
   body.

   THE FLAG IS SEEDED BEFORE THE LATCH IS TESTED.  MOV dword ptr [EBP-0x4],0x0
   at 000393bc runs ahead of the CMP at 000393c3, which is why it is written
   above the gate here rather than inside it.  Nothing observes the difference;
   the store is kept where its own assembly has it.

   THE POLL DOES NOT STOP AT THE FIRST SURVIVOR.  The JNZ at 000393f4 skips only
   the flag store and the loop runs its full seven passes either way, so every
   firing costs seven calls.  fdps_unit_is_retired only reads a record, so a
   rewrite that broke out early would differ in nothing but the call count --
   and it would still be a different function.

   THE SENSE OF THE TEST IS INVERTED.  TEST EAX,EAX / JNZ at 000393f2 raises the
   flag when fdps_unit_is_retired answers 0, so the flag means "one of the seven
   is still standing" and the draw is guarded by it being clear.

   THE RANGE IS SEVEN RECORDS FROM 0x50 AND IS NOT BOUNDED BY THE UNIT COUNT.
   Both numbers are literals; nothing in the body reads
   data_fdps_map_unit_count, so on a map with fewer records the loop would read
   past the live ones.  On MAP25.DAT, the only map whose data reaches this slot,
   they land on the wave-2 block exactly.

   THE INCOMING ARGUMENT IS NEVER READ.  MOV dword ptr [EBP+0x14],0x0 at
   000393cc is the seed of the 0..6 counter, which the original keeps in its own
   argument slot; the value fdps_run_death_scripts pushed -- the index of the
   unit that made the killing action, not that of the dead unit whose script is
   running -- is spent by that store and nothing loads the slot before it.  The
   counter is emitted as a local of its own here; nothing observes the
   difference.

   ONE VALUE IS USED AFTER A CALL.  fdps_unit_is_retired's answer comes back in
   EAX and is tested by the TEST EAX,EAX at 000393f2 that follows the ADD ESP
   cleaning its argument, so it is the call's own result and not a leftover.
   fdps_draw_text's cursor is discarded -- the next instruction after the CALL
   at 00039420 is the ADD ESP,0x1c that cleans its arguments.  Nothing sets EAX
   before the RET and no dispatcher reads what comes back, so the result is
   void. */
void fdps_chapter_26_event_wave_2_defeated_line(int unit_index)
{
    /* Raised when one of the seven answers the retirement test with 0, so the
       line is drawn only while it is still clear. */
    int any_wave_2_unit_still_standing;
    /* Which of the seven wave-2 records is being asked, 0 to 6. */
    int wave_2_slot;

    any_wave_2_unit_still_standing = 0;

    /* The original seeds that counter over its own argument slot at 000393cc,
       which is the only access the body makes to the slot; the counter is a
       local of its own here, so this is what is left of that store. */
    unit_index = 0;

    if (data_fdps_map_cell_event_triggered_flags[CH26_WIPE_ONE_SHOT_SLOT]
            == 0) {
        for (wave_2_slot = 0;
             wave_2_slot < CH26_WAVE_2_UNIT_COUNT;
             wave_2_slot++) {
            if (fdps_unit_is_retired(CH26_WAVE_2_FIRST_UNIT_INDEX
                                     + wave_2_slot) == 0) {
                any_wave_2_unit_still_standing = 1;
            }
        }

        if (any_wave_2_unit_still_standing == 0) {
            fdps_draw_text(data_fdps_current_chapter_text_ptr,
                           CH26_WIPE_TEXT_ID,
                           (unsigned char *) CH26_WIPE_TEXT_DEST,
                           VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                           MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
            data_fdps_map_cell_event_triggered_flags[
                CH26_WIPE_ONE_SHOT_SLOT] = 1;
        }
    }
}
