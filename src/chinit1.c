/* chinit1.c -- the per-chapter entry handlers, chapters 1 to 15: what the
 * game does at the moment it enters a chapter, before the battle loop runs.
 *
 * These are slots of the handler table based at 00060074, indexed by the
 * 0-based chapter id and called only through it, so the dispatcher in
 * title.c is not a static caller of any of them.
 *
 * See chinit1.h for what each handler sets up.  Nothing here owns state.
 */
#include "fdpstype.h"
#include "roster.h"
#include "chapter.h"
#include "icon.h"
#include "unit.h"
#include "mapcur.h"
#include "chinit1.h"

/* The character the game opens with: 蘭迪斯, roster character 0.  PUSH 0x0
   at 00020e9c. */
#define CH01_OPENING_CHARACTER 0

/* Chapter 1's opening cut-scene, the string at 0x61804 loaded into EAX at
   00020eab and pushed as fdps_icon_script_run's only argument.  The
   interpreter names IconAni.vfs itself and upper-cases the member name in
   place before the container compare (vfs.h, rebuild_info/pitfalls.md), so
   this literal is folded to ICON00.DAT by the call and cannot live in
   read-only storage.

   The number in the name is the 0-based chapter id and not a script id of
   its own: fdps_chapter_02_init holds Icon01.dat at the same position. */
#define CH01_OPENING_SCRIPT "Icon00.dat"

/* The guest ally the chapter opens around: map unit 2, the level-10 hero
   索爾, deployed by the cut-scene above out of map00.dat's single wave-1
   record.  PUSH 0x2 at 00020ebe. */
#define CH01_GUEST_HERO_UNIT 2

/* The poison counter as an index into struct fdps_unit_record's
   status_timers[]: [3] is record offset 0x25, the byte MOV byte ptr
   [EAX + 0x25],0xb at 00020ece writes.  The same slot combat.c's poison
   weapon effect assigns. */
#define POISON_TIMER_SLOT 3

/* What the two writes on him say.  Eleven turns of poison, and hp_current at
   +0x40 -- MOV word ptr [EAX + 0x40],0x64 -- put on 100 HP.  His maximum at
   +0x42 is not written and keeps what the deployment computed, which is what
   makes 100 the small fraction of his health the chapter's rescue is built
   around.  The store is a 16-bit one: widening it to the natural int spills
   into hp_max at +0x42 as well. */
#define CH01_GUEST_HERO_POISON_TURNS 0x0b
#define CH01_GUEST_HERO_OPENING_HP 100

/* Which unit the cursor is left on: PUSH 0x0 at 00020edb. */
#define CH01_CURSOR_UNIT 0

/* 00020e90.  Six calls and two stores, straight line, no branch and no loop.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP,
   MOV EBP,ESP at 00020e90..00020e94 -- over a four-byte local area, SUB
   ESP,0x4, and that one dword at [EBP-0x4] is the guest hero's record
   pointer and the only local the body has.

   CALLING CONVENTION.  Every argument goes on the stack and the caller takes
   it back: PUSH 0x0 / CALL 0x00023bc0 / ADD ESP,0x4 at 00020e9c, and the
   same shape at 00020eb0, 00020ebe and 00020edb.  The two argument-less
   calls at 00020ea6 and 00020eb9 are bare CALLs with no push and no
   adjustment.  Nothing reads [EBP+8] or above, so this function takes
   nothing itself, and EAX is never set before the RET at 00020eeb, so it
   returns nothing -- fdps_title_screen reaches it through the table slot at
   00060074 (COMPUTED_CALL at 0002a85e) and ignores EAX.

   VALUES USED AFTER A CALL, and what the assembly says each one is.  Exactly
   one: fdps_get_unit_record's EAX is stored at [EBP-0x4] (00020ec8) and
   re-read twice, at 00020ecb and 00020ed2, each time as the base of a field
   store.  It is a unit-record pointer.  The other five callees return void
   and nothing here reads EAX after any of them.

   THE ORDER IS THE ALGORITHM, and two steps of it are not visible in any
   data file.

   The roster add runs BEFORE the state reset.  fdps_chapter_state_reset
   deploys the map's player slots out of the roster array and stops at the
   roster count, so the intuitive "initialise the chapter, then add the
   character who joins it" order leaves 蘭迪斯 off chapter 1's map
   entirely.  fdps_title_screen has just reset the roster count to 0 on the
   new-game path, so slot 0 is where he lands.

   The two writes on 索爾 run AFTER the cut-scene.  The state reset leaves
   map00.dat's single player slot as the only unit in the array -- none of
   its 22 scripted deployments is tagged wave 0 -- and it is Icon00.dat that
   deploys wave 2 and then wave 1, which is what puts 索爾 at index 2.
   Performing the two stores before the script writes them into an array that
   is one record long.  Nor can the pair be dropped on the assumption that a
   unit's opening HP and status come from its deployment record: doing that
   opens chapter 1 with a healthy, unpoisoned guest hero and loses the timed
   rescue the chapter is built around.

   THE CHAPTER IS NOT SET HERE.  Both the script number and the title card
   are chosen from data_fdps_chapter_current_chapter_id by the callees, and
   this handler neither reads nor writes it -- the dispatcher that reached
   this slot is what put the right value there. */
void fdps_chapter_01_init(void)
{
    /* Map unit 2's record, the guest hero the two stores below land on.
       Resolved after the cut-scene, because the cut-scene is what deploys
       him. */
    struct fdps_unit_record *guest_hero;

    fdps_roster_add_character(CH01_OPENING_CHARACTER);
    fdps_chapter_state_reset();
    fdps_icon_script_run(CH01_OPENING_SCRIPT);
    fdps_show_chapter_title_card();

    guest_hero = fdps_get_unit_record(CH01_GUEST_HERO_UNIT);
    guest_hero->status_timers[POISON_TIMER_SLOT] =
        CH01_GUEST_HERO_POISON_TURNS;
    guest_hero->hp_current = CH01_GUEST_HERO_OPENING_HP;

    fdps_map_cursor_move_to_unit(CH01_CURSOR_UNIT);
}

/* --- fdps_chapter_02_init @ 00020ef0 ----------------------------------- */

/* The character who joins at the start of chapter 2: 尤利安 the 僧侶,
   character id 6 (assets/characters.md).  PUSH 0x6 at 00020efc.  He lands at
   roster slot 1, behind 蘭迪斯, because the roster is in join order and is
   never permuted -- the list chpost2.c reads off it by chapter 17 starts
   蘭迪斯, 尤利安, 亞克. */
#define CH02_JOINING_CHARACTER 6

/* Chapter 2's opening cut-scene, the string at 0x61810 loaded into EAX at
   00020f0b and pushed as fdps_icon_script_run's only argument.  The number in
   the name is the 0-based chapter id and not a script id of its own, which is
   why this is Icon01.dat and chapter 1's handler holds Icon00.dat at the same
   position.

   As with every member name, the interpreter names IconAni.vfs itself and
   upper-cases this string in place before the container compare (vfs.h,
   rebuild_info/pitfalls.md), so the literal is folded to ICON01.DAT by the
   call and cannot live in read-only storage. */
#define CH02_OPENING_SCRIPT "Icon01.dat"

/* Which unit the cursor is left on: PUSH 0x0 at 00020f1e. */
#define CH02_CURSOR_UNIT 0

/* 00020ef0.  Five calls, straight line, no branch, no loop and no local.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 00020ef0..00020ef4 -- over SUB ESP,0x0, a zero-byte local area
   written as the six-byte immediate form.  Nothing is addressed off EBP
   anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 00020f28..00020f2b
   and the RET at 00020f2c is bare.

   CALLING CONVENTION.  Every argument goes on the stack and the caller takes
   it back: PUSH 0x6 / CALL 0x00023bc0 / ADD ESP,0x4 at 00020efc, and the same
   shape at 00020f0b and 00020f1e.  The two argument-less calls at 00020f06
   and 00020f19 are bare CALLs with no push and no adjustment.  Nothing reads
   [EBP+8] or above, so this function takes nothing itself, and EAX is never
   set before the RET, so it returns nothing -- fdps_title_screen reaches it
   through the second slot of the table at 00060074 (the dword at 00060078 is
   00020ef0) and ignores EAX.

   VALUES USED AFTER A CALL: none at all.  All five callees return void and
   nothing here reads EAX after any of them; the only register written between
   the prologue and the RET is the EAX that carries the script name literal
   into the third call.

   THE ORDER IS THE ALGORITHM.  The roster add runs BEFORE the state reset:
   fdps_chapter_state_reset rebuilds the map unit array out of roster slot i
   for each of the map's player slots and stops at the roster count
   (src/deploy.c), so the intuitive "initialise the chapter, then add the
   character who joins it" order leaves 尤利安 off chapter 2's map and turns
   his slot into the zeroed, retired spare the rebuild writes for a player slot
   with no member behind it.  MAP01.DAT declares two player slots, and 尤利安
   is the second of the two the party has by then.

   Nor is the add guarded: fdps_roster_add_character appends at the current
   count and increments unconditionally, so entering this handler twice puts
   him on the roster twice.  Adding a duplicate test here is a check the
   original does not have.

   Unlike chapter 1's handler this one writes nothing on a unit afterwards --
   there are no stores in the body at all, only the five calls -- so chapter 2
   opens every unit on whatever the deployment computed.

   THE CHAPTER IS NOT SET HERE.  Both the script the third call loads and the
   title-card graphic the fourth one shows are chosen from
   data_fdps_chapter_current_chapter_id by the callees, and this handler
   neither reads nor writes it -- the dispatcher that reached this slot is what
   put the right value there.  The Icon01.dat above is the one place the
   chapter number is spelled out rather than read. */
void fdps_chapter_02_init(void)
{
    fdps_roster_add_character(CH02_JOINING_CHARACTER);
    fdps_chapter_state_reset();
    fdps_icon_script_run(CH02_OPENING_SCRIPT);
    fdps_show_chapter_title_card();
    fdps_map_cursor_move_to_unit(CH02_CURSOR_UNIT);
}

/* --- fdps_chapter_03_init @ 00020f30 ----------------------------------- */

/* The character who joins at the start of chapter 3: 亞克 the 騎士,
   character id 4 (assets/characters.md).  PUSH 0x4 at 00020f3c.  He lands at
   roster slot 2, behind 蘭迪斯 and 尤利安, because the roster is in join
   order and is never permuted -- and MAP02.DAT asks for exactly three player
   slots, which is the party this add completes.  The strategy guide's opening
   line for the chapter is the same character: LV7 騎士亞克 at 124 HP, which
   is FRIAPRDA.DAT's 64 base plus FRILEVUP.DAT's 10 a level over his six
   levels. */
#define CH03_JOINING_CHARACTER 4

/* Chapter 3's opening cut-scene, the string at 0x6181c loaded into EAX at
   00020f4b and pushed as fdps_icon_script_run's only argument.  The number in
   the name is the 0-based chapter id and not a script id of its own, which is
   why this is Icon02.dat and the two handlers above hold Icon00.dat and
   Icon01.dat at the same position.

   As with every member name, the interpreter names IconAni.vfs itself and
   upper-cases this string in place before the container compare (vfs.h,
   rebuild_info/pitfalls.md), so the literal is folded to ICON02.DAT by the
   call and cannot live in read-only storage. */
#define CH03_OPENING_SCRIPT "Icon02.dat"

/* Which unit the cursor is left on: PUSH 0x0 at 00020f5e. */
#define CH03_CURSOR_UNIT 0

/* 00020f30.  Five calls, straight line, no branch, no loop and no local --
   the same shape as chapter 2's handler with two different arguments.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 00020f30..00020f34 -- over SUB ESP,0x0, a zero-byte local area
   written as the six-byte immediate form.  Nothing is addressed off EBP
   anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 00020f68..00020f6b
   and the RET at 00020f6c is bare.

   CALLING CONVENTION.  Every argument goes on the stack and the caller takes
   it back: PUSH 0x4 / CALL 0x00023bc0 / ADD ESP,0x4 at 00020f3c, and the same
   shape at 00020f4b and 00020f5e.  The two argument-less calls at 00020f46
   and 00020f59 are bare CALLs with no push and no adjustment.  Nothing reads
   [EBP+8] or above, so this function takes nothing itself, and EAX is never
   set before the RET, so it returns nothing -- the dispatcher reaches it
   through the third slot of the table at 00060074 (the dword at 0006007c is
   00020f30) and ignores EAX.

   VALUES USED AFTER A CALL: none at all.  All five callees return void and
   nothing here reads EAX after any of them; the only register written between
   the prologue and the RET is the EAX that carries the script name literal
   into the third call.

   THE ORDER IS THE ALGORITHM.  The roster add runs BEFORE the state reset:
   fdps_chapter_state_reset rebuilds the map unit array out of roster slot i
   for each of the map's player slots and stops at the roster count
   (src/deploy.c), so the intuitive "initialise the chapter, then add the
   character who joins it" order leaves 亞克 off chapter 3's map and turns his
   slot into the zeroed, retired spare the rebuild writes for a player slot
   with no member behind it.  MAP02.DAT declares three player slots and the
   party is 蘭迪斯, 尤利安, 亞克 -- the add is what makes the third of the
   three a live unit rather than that spare.

   Nor is the add guarded: fdps_roster_add_character appends at the current
   count and increments unconditionally, so entering this handler twice puts
   him on the roster twice.  Adding a duplicate test here is a check the
   original does not have.

   WHY THIS HANDLER WRITES NOTHING ON A UNIT, unlike chapter 1's.  Chapter 3
   opens with a guest hero as well -- 索爾, side 1, level 10 -- but he is
   MAP02.DAT's own wave-0 record and so is deployed by the state reset out of
   the map file, at the health his deployment computes.  Chapter 1's 索爾 came
   out of the cut-scene and had to be given his poison and his 100 HP by hand;
   here there is nothing left for the handler to say, and the body has no
   store in it at all.

   THE CHAPTER IS NOT SET HERE.  Both the script the third call loads and the
   title-card graphic the fourth one shows are chosen from
   data_fdps_chapter_current_chapter_id by the callees, and this handler
   neither reads nor writes it -- the dispatcher that reached this slot is what
   put the right value there.  The Icon02.dat above is the one place the
   chapter number is spelled out rather than read. */
void fdps_chapter_03_init(void)
{
    fdps_roster_add_character(CH03_JOINING_CHARACTER);
    fdps_chapter_state_reset();
    fdps_icon_script_run(CH03_OPENING_SCRIPT);
    fdps_show_chapter_title_card();
    fdps_map_cursor_move_to_unit(CH03_CURSOR_UNIT);
}
