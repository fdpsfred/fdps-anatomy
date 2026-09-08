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
