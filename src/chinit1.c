/* chinit1.c -- the per-chapter entry handlers, chapters 1 to 10: what the
 * game does at the moment it enters a chapter, before the battle loop runs.
 * Chapters 11 to 15 are chinit1b.c, 16 to 24 are chinit2.c and 25 to 30 are
 * chinit2b.c.
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

   The roster add runs BEFORE the state reset, but only its place ahead of the
   cut-scene is load-bearing.  fdps_chapter_state_reset deploys the map's
   player slots out of the roster array and stops at the roster count, yet the
   array this handler's own reset builds does not survive: Icon00.dat opens
   with SWITCH_MAP 0x20 at script offset 0x000, and its SWITCH_MAP 0x00 at
   offset 0x51c calls fdps_chapter_state_reset again and rebuilds map 0 out of
   the roster.  So an add moved behind the reset but still ahead of
   fdps_icon_script_run puts 蘭迪斯 on the map all the same; only an add after
   the cut-scene leaves him off chapter 1's map entirely, and the order here is
   simply the original's.  fdps_title_screen has just reset the roster count to
   0 on the new-game path, so slot 0 is where he lands.

   The two writes on 索爾 run AFTER the cut-scene.  The rebuild at script
   offset 0x51c leaves map00.dat's single player slot as the only unit in the array -- none of
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

/* --- fdps_chapter_04_init @ 00020f70 ----------------------------------- */

/* The character who joins at the start of chapter 4: 法蓮娜 the 魔導士,
   character id 1 (assets/characters.md).  PUSH 0x1 at 00020f7c.  She lands at
   roster slot 3, behind 蘭迪斯, 尤利安 and 亞克, because the roster is in
   join order and is never permuted -- chapters 1 to 3 are the only other
   callers of fdps_roster_add_character the game reaches before this one. */
#define CH04_JOINING_CHARACTER 1

/* Chapter 4's opening cut-scene, the string at 0x61828 loaded into EAX at
   00020f8b and pushed as fdps_icon_script_run's only argument.  The number in
   the name is the 0-based chapter id and not a script id of its own, which is
   why this is Icon03.dat where the three handlers above hold Icon00.dat,
   Icon01.dat and Icon02.dat at the same position.

   As with every member name, the interpreter names IconAni.vfs itself and
   upper-cases this string in place before the container compare (vfs.h,
   rebuild_info/pitfalls.md), so the literal is folded to ICON03.DAT by the
   call and cannot live in read-only storage. */
#define CH04_OPENING_SCRIPT "Icon03.dat"

/* Which unit the cursor is left on: PUSH 0x0 at 00020f9e. */
#define CH04_CURSOR_UNIT 0

/* 00020f70.  Five calls, straight line, no branch, no loop and no local --
   byte for byte the shape of chapter 3's handler with two different
   arguments.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 00020f70..00020f74 -- over SUB ESP,0x0, a zero-byte local area
   written as the six-byte immediate form.  Nothing is addressed off EBP
   anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 00020fa8..00020fab
   and the RET at 00020fac is bare.

   CALLING CONVENTION.  Every argument goes on the stack and the caller takes
   it back: PUSH 0x1 / CALL 0x00023bc0 / ADD ESP,0x4 at 00020f7c, and the same
   shape at 00020f8b and 00020f9e.  The two argument-less calls at 00020f86
   and 00020f99 are bare CALLs with no push and no adjustment.  Nothing reads
   [EBP+8] or above, so this function takes nothing itself, and EAX is never
   set before the RET, so it returns nothing -- the dispatcher reaches it
   through the fourth slot of the table at 00060074 (the dword at 00060080 is
   00020f70) and ignores EAX.

   VALUES USED AFTER A CALL: none at all.  All five callees return void and
   nothing here reads EAX after any of them; the only register written between
   the prologue and the RET is the EAX that carries the script name literal
   into the third call.

   THE ORDER IS THE ORIGINAL'S AND IS KEPT, BUT CHAPTER 4 IS WHERE IT STOPS
   BEING VISIBLE.  The add still runs before the reset, as it does in the three
   handlers above.  What it no longer decides is chapter 4's deployment:
   MAP03.DAT declares three player slots, the party already has three members
   when the chapter opens -- 蘭迪斯, 尤利安 and 亞克, one from each of the
   chapters before it -- and fdps_build_map_unit_array fills slot i from roster
   slot i only while i is below the roster count (src/deploy.c).  法蓮娜 is
   roster slot 3, one past the last slot the map asks for, so she is not one of
   chapter 4's map units whichever way round the two calls run.

   She reaches the map by the other road, and it is the third call above that
   opens it: MAP03.DAT's deployment record 21 is side 2, character 1, level 8
   and is tagged wave 3, and the shipped ICON03.DAT asks for exactly that wave
   -- walked with the opcode ladder in src/icon.c its bytes are SET_MUSIC,
   SET_VIEW_TILE, FACE_UNITS, PLAY_SAF and then DEPLOY_WAVE 3 with exact
   placement at script offset 12.  So this handler does put her on the map; it
   does it through the cut-scene and not through the roster add.

   That deployment record and not the add is also where her level comes from --
   the add builds her roster record from FRIAPRDA.DAT's level 3, and it is the
   wave-3 record that makes the level-8 魔導士 the strategy guide lists for
   this chapter (assets/characters.md).

   WHAT THE MAP DEPLOYS ON ITS OWN.  MAP03.DAT tags exactly one of its 33
   records wave 0 -- record 0, side 1, character 12, level 10, the guest hero
   索爾 again -- so the state reset puts him down behind the three player slots
   and this handler has no store in its body to make on him, exactly as chapter
   3's has none.

   THE CHAPTER IS NOT SET HERE.  Both the script the third call loads and the
   title-card graphic the fourth one shows are chosen from
   data_fdps_chapter_current_chapter_id by the callees, and this handler
   neither reads nor writes it -- the dispatcher that reached this slot is what
   put the right value there.  The Icon03.dat above is the one place the
   chapter number is spelled out rather than read. */
void fdps_chapter_04_init(void)
{
    fdps_roster_add_character(CH04_JOINING_CHARACTER);
    fdps_chapter_state_reset();
    fdps_icon_script_run(CH04_OPENING_SCRIPT);
    fdps_show_chapter_title_card();
    fdps_map_cursor_move_to_unit(CH04_CURSOR_UNIT);
}

/* --- fdps_chapter_05_init @ 00020fb0 ----------------------------------- */

/* Chapter 5's opening cut-scene, the string at 0x61834 loaded into EAX at
   00020fc1 and pushed as fdps_icon_script_run's only argument.  The number in
   the name is the 0-based chapter id and not a script id of its own, which is
   why this is Icon04.dat where the four handlers above hold Icon00.dat to
   Icon03.dat at the same position.

   As with every member name, the interpreter names IconAni.vfs itself and
   upper-cases this string in place before the container compare (vfs.h,
   rebuild_info/pitfalls.md), so the literal is folded to ICON04.DAT by the
   call and cannot live in read-only storage. */
#define CH05_OPENING_SCRIPT "Icon04.dat"

/* Which unit the cursor is left on: PUSH 0x0 at 00020fd4. */
#define CH05_CURSOR_UNIT 0

/* 00020fb0.  Four calls, straight line, no branch, no loop and no local --
   the handlers above with their fdps_roster_add_character taken off the
   front, and the first slot of the table where that call is absent.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 00020fb0..00020fb4 -- over SUB ESP,0x0, a zero-byte local area
   written as the six-byte immediate form.  Nothing is addressed off EBP
   anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 00020fde..00020fe1
   and the RET at 00020fe2 is bare.

   CALLING CONVENTION.  Every argument goes on the stack and the caller takes
   it back: MOV EAX,0x61834 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   00020fc1..00020fcc, and PUSH 0x0 / CALL 0x0002da50 / ADD ESP,0x4 at
   00020fd4..00020fdb.  The two argument-less calls at 00020fbc and 00020fcf
   are bare CALLs with no push and no adjustment.  Nothing reads [EBP+8] or
   above, so this function takes nothing itself, and EAX is never set after
   the third call, so it returns nothing -- the dispatcher reaches it through
   the fifth slot of the table at 00060074 (the dword at 00060084 is
   00020fb0) and ignores EAX.

   VALUES USED AFTER A CALL: none at all.  All four callees return void and
   nothing here reads EAX after any of them; the only register written between
   the prologue and the RET is the EAX that carries the script name literal
   into the second call.

   NOBODY JOINS THE PARTY THIS CHAPTER, and the absence is the content.  The
   four handlers above each open with fdps_roster_add_character; the first
   instruction after this prologue is the state rebuild, so the roster is left
   exactly as chapter 4 finished it -- 蘭迪斯, 尤利安, 亞克, 法蓮娜, four
   members.  MAP04.DAT asks for FIVE player slots, and
   fdps_build_map_unit_array fills slot i from roster slot i only while i is
   below the roster count (src/deploy.c), so chapter 5 opens with slot 4 as
   the zeroed, retired spare that rebuild writes for a slot with no member
   behind it.  A handler that added a fifth character here to fill the map's
   fifth slot would be a party the game does not have.

   WHAT WRITES ON A UNIT INSTEAD.  The strategy guide lists chapter 5's ally
   as LV10 英雄索爾 in 麻痺狀態, and neither half of that comes from this
   function: MAP04.DAT's own deployment record 30 -- side 1, character 12,
   level 10 -- is the map's single wave-0 record, so the state rebuild puts
   him down as unit 5 behind the five player slots, and the shipped
   ICON04.DAT is what paralyses him.  Walked with the opcode ladder in
   src/icon.c its SET_UNIT_TIMER at script offset 74 names unit 5, timer slot
   operand 1 -- status_timers[4], the paralysis counter -- and value 0xff.
   The body here has no store in it at all, exactly as chapters 2 to 4 have
   none, and chapter 1's pair of stores is the family's exception rather than
   its shape.

   THE CHAPTER IS NOT SET HERE.  Both the script the second call loads and the
   title-card graphic the third one shows are chosen from
   data_fdps_chapter_current_chapter_id by the callees, and this handler
   neither reads nor writes it -- the dispatcher that reached this slot is what
   put the right value there.  The Icon04.dat above is the one place the
   chapter number is spelled out rather than read. */
void fdps_chapter_05_init(void)
{
    fdps_chapter_state_reset();
    fdps_icon_script_run(CH05_OPENING_SCRIPT);
    fdps_show_chapter_title_card();
    fdps_map_cursor_move_to_unit(CH05_CURSOR_UNIT);
}

/* --- fdps_chapter_06_init @ 00020ff0 ----------------------------------- */

/* Chapter 6's opening cut-scene, the string at 0x61840 loaded into EAX at
   00021001 and pushed as fdps_icon_script_run's only argument.  The number in
   the name is the 0-based chapter id and not a script id of its own, which is
   why this is Icon05.dat where fdps_chapter_05_init above holds Icon04.dat at
   the same position.

   As with every member name, the interpreter names IconAni.vfs itself and
   upper-cases this string in place before the container compare (vfs.h,
   rebuild_info/pitfalls.md), so the literal is folded to ICON05.DAT by the
   call and cannot live in read-only storage. */
#define CH06_OPENING_SCRIPT "Icon05.dat"

/* Which unit the cursor is left on: PUSH 0x0 at 00021014. */
#define CH06_CURSOR_UNIT 0

/* 00020ff0.  Four calls, straight line, no branch, no loop and no local --
   instruction for instruction the same shape as fdps_chapter_05_init above,
   with the script-name literal and nothing else changed.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 00020ff0..00020ff4 -- over SUB ESP,0x0, a zero-byte local area
   written as the six-byte immediate form.  Nothing is addressed off EBP
   anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 0002101e..00021021
   and the RET at 00021022 is bare.

   CALLING CONVENTION.  Every argument goes on the stack and the caller takes
   it back: MOV EAX,0x61840 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   00021001..0002100c, and PUSH 0x0 / CALL 0x0002da50 / ADD ESP,0x4 at
   00021014..0002101b.  The two argument-less calls at 00020ffc and 0002100f
   are bare CALLs with no push and no adjustment.  Nothing reads [EBP+8] or
   above, so this function takes nothing itself, and EAX is never set after
   the third call, so it returns nothing -- the dispatcher reaches it through
   the sixth slot of the table at 00060074 (the dword at 00060088 is
   00020ff0) and ignores EAX.

   VALUES USED AFTER A CALL: none at all.  All four callees return void and
   nothing here reads EAX after any of them; the only register written between
   the prologue and the RET is the EAX that carries the script name literal
   into the second call.

   NOBODY JOINS THE PARTY THIS CHAPTER EITHER, and this is where that stops
   being a shortfall.  The party is the four members chapters 1 to 4 added --
   蘭迪斯, 尤利安, 亞克, 法蓮娜 -- and MAP05.DAT asks for exactly FOUR player
   slots, so every slot has a member behind it and fdps_build_map_unit_array
   writes no retired spare at all (src/deploy.c).  Chapter 5's map wanted five
   against the same four; this one is the first map in the run whose slot count
   and party size agree.

   THE CUT-SCENE IS WHAT BRINGS THE ARMY IN, AND THE ORDER OF THE FIRST TWO
   CALLS IS THEREFORE LOAD-BEARING.  MAP05.DAT's 32 deployment records are
   tagged in three waves: two wave 0, twenty-three wave 1, seven wave 2.  The
   state rebuild's own opening deploy takes wave 0 alone, which is record 0 --
   a side-0 LV10 步兵 -- and record 31, the side-1 LV10 英雄索爾 the strategy
   guide lists as the chapter's 友方; the units land in record order behind the
   four player slots, so 索爾 is unit 5.  The shipped ICON05.DAT's one
   DEPLOY_WAVE, at script offset 83 walked with the opcode ladder in src/icon.c,
   asks for wave 1, so the twenty-three arrive during the cut-scene and the
   handler returns with 29 units on the map.  The seven wave-2 records are
   nobody's business here: nothing this handler calls deploys them.

   WHY THE GUIDE PRINTS THE ENEMIES AS TWO GROUPS.  The strategy guide lists
   LV10 魔導士 x3, LV10 步兵 x9, LV9 弓兵 x7 and LV8 騎兵 x5, then a second
   group of x1, x3, x2 and x1.  The first group is exactly the map's wave 0 and
   wave 1 counted together by character id -- 102 three times, 98 nine times, 93
   seven times, 88 five times -- and the second is exactly its wave 2.  The
   split in the guide is the map's own wave tagging, so the twenty-four the
   handler leaves behind is the number a player meets when the chapter opens and
   not a shortfall.

   NOTHING IS PARALYSED THIS CHAPTER.  Chapter 5's guest hero arrives in
   麻痺狀態 and it is the shipped ICON05.DAT's predecessor that does it; walked
   with the opcode ladder in src/icon.c the shipped ICON05.DAT holds no
   SET_UNIT_TIMER at all, and the guide lists 索爾 here with no status.  So the
   absence of a store in this body is not a gap the cut-scene fills -- neither
   half writes on a unit in chapter 6.

   THE CHAPTER IS NOT SET HERE.  Both the script the second call loads and the
   title-card graphic the third one shows are chosen from
   data_fdps_chapter_current_chapter_id by the callees, and this handler
   neither reads nor writes it -- the dispatcher that reached this slot is what
   put the right value there.  The Icon05.dat above is the one place the
   chapter number is spelled out rather than read. */
void fdps_chapter_06_init(void)
{
    fdps_chapter_state_reset();
    fdps_icon_script_run(CH06_OPENING_SCRIPT);
    fdps_show_chapter_title_card();
    fdps_map_cursor_move_to_unit(CH06_CURSOR_UNIT);
}

/* --- fdps_chapter_07_init @ 00021030 ----------------------------------- */

/* The character who joins the party at the start of chapter 7: 裘娜 the 戰士,
   character id 3 (assets/characters.md).  PUSH 0x3 at 0002103c.  She lands at
   roster slot 4, behind 蘭迪斯, 尤利安, 亞克 and 法蓮娜, because the roster is
   in join order and is never permuted and because chapters 5 and 6 added
   nobody.

   The strategy guide's 加入 line for this chapter is the same record read from
   the other side: LV15 戰士裘娜, HP225, 鐵刀 and 青鎧甲.  That is exactly what
   this add computes -- FRIAPRDA.DAT's level 15 and 85 base HP plus
   FRILEVUP.DAT's 10 HP a level over her fourteen levels is 225, and the two
   item ids on her line are 0x0f and 0x66 (assets/items.md). */
#define CH07_JOINING_CHARACTER 3

/* Chapter 7's opening cut-scene, the string at 0x6184c loaded into EAX at
   0002104b and pushed as fdps_icon_script_run's only argument.  The number in
   the name is the 0-based chapter id and not a script id of its own, which is
   why this is Icon06.dat where fdps_chapter_06_init above holds Icon05.dat at
   the same position.

   As with every member name, the interpreter names IconAni.vfs itself and
   upper-cases this string in place before the container compare (vfs.h,
   rebuild_info/pitfalls.md), so the literal is folded to ICON06.DAT by the
   call and cannot live in read-only storage. */
#define CH07_OPENING_SCRIPT "Icon06.dat"

/* Which unit the cursor is left on: PUSH 0x0 at 0002105e. */
#define CH07_CURSOR_UNIT 0

/* 00021030.  Five calls, straight line, no branch, no loop and no local --
   the plain form of fdps_chapter_05_init and fdps_chapter_06_init with an
   fdps_roster_add_character put back in front of it, which is the shape
   chapters 2 to 4 have.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 00021030..00021034 -- over SUB ESP,0x0, a zero-byte local area
   written as the six-byte immediate form.  Nothing is addressed off EBP
   anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 00021068..0002106b
   and the RET at 0002106c is bare.

   CALLING CONVENTION.  Every argument goes on the stack and the caller takes
   it back: PUSH 0x3 / CALL 0x00023bc0 / ADD ESP,0x4 at 0002103c..00021043,
   MOV EAX,0x6184c / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   0002104b..00021056, and PUSH 0x0 / CALL 0x0002da50 / ADD ESP,0x4 at
   0002105e..00021065.  The two argument-less calls at 00021046 and 00021059
   are bare CALLs with no push and no adjustment.  Nothing reads [EBP+8] or
   above, so this function takes nothing itself, and EAX is never set after the
   third call, so it returns nothing -- the dispatcher reaches it through the
   seventh slot of the table at 00060074 (the dword at 0006008c is 00021030)
   and ignores EAX.

   VALUES USED AFTER A CALL: none at all.  All five callees return void and
   nothing here reads EAX after any of them; the only register written between
   the prologue and the RET is the EAX that carries the script name literal
   into the third call.

   THE ADD IS NOT WHAT PUTS ANYBODY ON THIS CHAPTER'S MAP, and chapter 7 is the
   second handler where that is so.  MAP06.DAT declares FOUR player slots, the
   party already has four members when the chapter opens -- 蘭迪斯, 尤利安,
   亞克 and 法蓮娜, chapters 5 and 6 having added none -- and
   fdps_build_map_unit_array fills slot i from roster slot i only while i is
   below the roster count (src/deploy.c).  裘娜 is roster slot 4, one past the
   last slot the map asks for, so she is not one of chapter 7's map units
   whichever way round the first two calls run.  MAP07.DAT declares five, so
   chapter 8 is the first map she is deployed on; the order is kept because it
   is the original's and not because this chapter shows it.

   SHE IS ON THE MAP ALL THE SAME, AS THE ENEMY, AND THAT IS A DIFFERENT
   RECORD.  MAP06.DAT scripts five deployments and none of them is a roster
   character: record 0 is side 0, character 116, level 15, and records 1 to 4
   are side 0, character 86, level 14.  Those are the strategy guide's 敵方
   line for the chapter to the number -- LV15 裘娜 and LV14 傭兵 x4 -- and what
   ties character 116 to the 裘娜 the add is about is her equipment, the same
   0x0f 鐵刀 and 0x66 青鎧甲 pair FRIAPRDA.DAT gives roster character 3.  The
   roster record and the enemy record are two different characters as far as
   the program is concerned, and both exist at once during this chapter.

   THE WHOLE OPPOSITION COMES FROM THE CUT-SCENE.  MAP06.DAT tags NO record
   wave 0 at all, so fdps_chapter_state_reset's own opening deploy matches
   nothing and the array it leaves is the four player slots and no more.  The
   shipped ICON06.DAT is what brings the five on: walked with the opcode ladder
   in src/icon.c it holds DEPLOY_WAVE 1 at script offset 228, which is the four
   傭兵, and DEPLOY_WAVE 2 at script offset 274, which is 裘娜, both with the
   place-exact operand 0.  The handler therefore returns with nine units on the
   map, which is exactly as many placement records as MAP06.COD carries: 63
   bytes is nine records behind its nine-byte header, five for the scripted
   deployments and four for the party's start tiles.

   THE ARRAY THE CHAPTER IS PLAYED ON IS NOT THE ONE THIS HANDLER'S OWN RESET
   BUILT.  ICON06.DAT's second opcode, at script offset 2, is SWITCH_MAP 0x2f,
   which sets the chapter id to 47 and rebuilds on MAP47.DAT -- a cut-scene map
   with no player slot and fourteen scripted deployments -- and its SWITCH_MAP
   0x06 at script offset 210 sets the id back to 6 and rebuilds map 6 again
   before either DEPLOY_WAVE runs.  So the chapter id is the 6 it entered with
   by the time the title card is shown, and the roster add still has to precede
   a reset because the reset that matters reads the roster the same way.

   NOTHING IS WRITTEN ON A UNIT.  The body has no store in it at all, as
   chapters 2 to 6 have none, and the cut-scene adds nothing either: walked
   with the same ladder the shipped ICON06.DAT holds no SET_UNIT_TIMER
   anywhere in its 557 bytes.  Chapter 7 opens with every unit on whatever its
   deployment computed, and the guide lists no status on any of them.

   THE CHAPTER IS NOT SET HERE.  Both the script the third call loads and the
   title-card graphic the fourth one shows are chosen from
   data_fdps_chapter_current_chapter_id by the callees, and this handler
   neither reads nor writes it -- the dispatcher that reached this slot is what
   put the right value there.  The Icon06.dat above is the one place the
   chapter number is spelled out rather than read. */
void fdps_chapter_07_init(void)
{
    fdps_roster_add_character(CH07_JOINING_CHARACTER);
    fdps_chapter_state_reset();
    fdps_icon_script_run(CH07_OPENING_SCRIPT);
    fdps_show_chapter_title_card();
    fdps_map_cursor_move_to_unit(CH07_CURSOR_UNIT);
}

/* --- fdps_chapter_08_init @ 00021070 ----------------------------------- */

/* The character who joins the party at the start of chapter 8: 費塔加 the
   魔導士, character id 2 (assets/characters.md).  PUSH 0x2 at 0002107c.  He
   lands at roster slot 5, behind 蘭迪斯, 尤利安, 亞克, 法蓮娜 and 裘娜,
   because the roster is in join order and is never permuted.

   THE RECORD THE ADD BUILDS IS NOT THE ONE THE STRATEGY GUIDE PRINTS, and
   chapter 8 is the first chapter where the two differ.
   fdps_roster_add_character reads FRIAPRDA.DAT index 02 and FRILEVUP.DAT
   index 02, which is level 13 on 78 base HP and 65 base MP with 6 HP and 7 MP
   a level, so the roster record is LV13 at 150 HP and 149 MP carrying 魔法之杖
   and 祭司袍.  The guide's 加入 line for the chapter is LV15 at HP162 and
   MP163 carrying 光之杖 and 祭司袍, and that is MAP07.DAT's own deployment 19
   -- side 1, character 2, level 15, item ids 0x33 and 0x84 -- read through
   fdps_deploy_unit instead: the same two tables at level 15 give 78 + 6 * 14 =
   162, 65 + 7 * 14 = 163 and a DX of 8 + 2 * 15 = 38, which are the guide's
   three numbers exactly.  The guide's 加入 line for this chapter is its own
   友軍 line repeated, so both describe the map record. */
#define CH08_JOINING_CHARACTER 2

/* Chapter 8's opening cut-scene, the string at 0x61858 loaded into EAX at
   0002108b and pushed as fdps_icon_script_run's only argument.  The number in
   the name is the 0-based chapter id and not a script id of its own, which is
   why this is Icon07.dat where fdps_chapter_07_init above holds Icon06.dat at
   the same position.

   As with every member name, the interpreter names IconAni.vfs itself and
   upper-cases this string in place before the container compare (vfs.h,
   rebuild_info/pitfalls.md), so the literal is folded to ICON07.DAT by the
   call and cannot live in read-only storage. */
#define CH08_OPENING_SCRIPT "Icon07.dat"

/* Which unit the cursor is left on: PUSH 0x0 at 0002109e. */
#define CH08_CURSOR_UNIT 0

/* 00021070.  Five calls, straight line, no branch, no loop and no local --
   the same shape as fdps_chapter_07_init above, with a different character id
   and a different script name and nothing else changed.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 00021070..00021074 -- over SUB ESP,0x0, a zero-byte local area
   written as the six-byte immediate form.  Nothing is addressed off EBP
   anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 000210a8..000210ab
   and the RET at 000210ac is bare.

   CALLING CONVENTION.  Every argument goes on the stack and the caller takes
   it back: PUSH 0x2 / CALL 0x00023bc0 / ADD ESP,0x4 at 0002107c..00021083,
   MOV EAX,0x61858 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   0002108b..00021096, and PUSH 0x0 / CALL 0x0002da50 / ADD ESP,0x4 at
   0002109e..000210a5.  The two argument-less calls at 00021086 and 00021099
   are bare CALLs with no push and no adjustment.  Nothing reads [EBP+8] or
   above, so this function takes nothing itself, and EAX is never set after the
   third call, so it returns nothing -- the dispatcher reaches it through the
   eighth slot of the table at 00060074 (the dword at 00060090 is 00021070)
   and ignores EAX.

   VALUES USED AFTER A CALL: none at all.  All five callees return void and
   nothing here reads EAX after any of them; the only register written between
   the prologue and the RET is the EAX that carries the script name literal
   into the third call.

   THE ADD IS NOT WHAT PUTS 費塔加 ON THIS CHAPTER'S MAP, and chapter 8 is the
   third handler where that is so.  MAP07.DAT declares FIVE player slots, the
   party already has five members when the chapter opens -- 蘭迪斯, 尤利安,
   亞克, 法蓮娜 and 裘娜 -- and fdps_build_map_unit_array fills slot i from
   roster slot i only while i is below the roster count (src/deploy.c).
   費塔加 is roster slot 5, one past the last slot the map asks for, so he is
   not one of chapter 8's map units whichever way round the first two calls
   run.  Chapter 7 is where MAP06.DAT's four slots left 裘娜 over in the same
   way; here every slot the map asks for has a member behind it and none of
   them is the retired spare.

   HE IS ON THE MAP ALL THE SAME, AS A SIDE-1 ALLY, AND THAT IS A DIFFERENT
   RECORD -- MAP07.DAT's deployment 19, the LV15 費塔加 the note on
   CH08_JOINING_CHARACTER above takes apart.  It is tagged wave 1, and nothing
   this handler runs deploys wave 1 on map 7, so the record is still waiting
   when the handler returns; the chapter's turn-scheduled event
   fdps_chapter_08_event_for_turn (000372d0) brings him on later, playing
   Icon7-1.dat on turn 3, whose DEPLOY_WAVE deploys wave 1.  The roster record and the map
   record are two different characters as far as the program is concerned.

   THE CUT-SCENE SPENDS ITSELF ON A DIFFERENT MAP AND PUTS THIS ONE BACK.
   Walked with the opcode ladder in src/icon.c, the shipped ICON07.DAT's second
   opcode, at script offset 2, is SWITCH_MAP 0x30, which sets the chapter id to
   48 and rebuilds on MAP48.DAT -- a cut-scene map with no player slot at all
   whose six deployments are the five party members at level 2 and, tagged wave
   1, 費塔加 at level 2.  Its one DEPLOY_WAVE, at script offset 107, is wave 1
   with the place-exact operand 1, so it is that sixth actor it brings on, and
   the walking, retiring and reviving that follow are all on him.  SWITCH_MAP
   0x07 at script offset 160 then sets the chapter id back to 7 and rebuilds
   map 7 before the script ends, and no DEPLOY_WAVE follows it.

   So the array the handler returns with is the one that second switch built:
   five player slots and the fourteen records MAP07.DAT tags wave 0, for
   nineteen.  That the roster add still has to precede a reset is unchanged --
   the reset that matters reads the roster the same way -- and the chapter id
   the title card is chosen from is the 7 it entered with, because the script
   put it back.

   NOTHING IS WRITTEN ON A UNIT.  The body has no store in it at all, as
   chapters 2 to 7 have none, and the cut-scene adds nothing either: walked
   with the same ladder the shipped ICON07.DAT holds no SET_UNIT_TIMER anywhere
   in its 251 bytes.

   THE CHAPTER IS NOT SET HERE.  Both the script the third call loads and the
   title-card graphic the fourth one shows are chosen from
   data_fdps_chapter_current_chapter_id by the callees, and this handler
   neither reads nor writes it -- the dispatcher that reached this slot is what
   put the right value there, and the cut-scene's own pair of switches leaves
   it as it found it.  The Icon07.dat above is the one place the chapter number
   is spelled out rather than read. */
void fdps_chapter_08_init(void)
{
    fdps_roster_add_character(CH08_JOINING_CHARACTER);
    fdps_chapter_state_reset();
    fdps_icon_script_run(CH08_OPENING_SCRIPT);
    fdps_show_chapter_title_card();
    fdps_map_cursor_move_to_unit(CH08_CURSOR_UNIT);
}

/* --- fdps_chapter_09_init @ 000210b0 ----------------------------------- */

/* The two characters who join the party at the start of chapter 9: 布蘭多 the
   技師, character id 8, and 蓋亞 the 機兵, character id 9
   (assets/characters.md).  PUSH 0x8 at 000210bc and PUSH 0x9 at 000210c6.
   They land at roster slots 6 and 7, behind 蘭迪斯, 尤利安, 亞克, 法蓮娜,
   裘娜 and 費塔加, because the roster is in join order and is never permuted.

   THE RECORDS THESE TWO ADDS BUILD ARE THE STRATEGY GUIDE'S OWN LINE FOR THE
   CHAPTER, which is what separates chapter 9 from chapter 8.
   fdps_roster_add_character reads FRIAPRDA.DAT and FRILEVUP.DAT at index 08 --
   level 14 on 50 base HP and 0 base MP with 9 HP and 3 MP a level -- and at
   index 09 -- level 16 on 60 base HP and 0 base MP with 12 HP and 3 MP a level
   -- so the two roster records carry 50 + 9 * 13 = 167 HP with 3 * 13 = 39 MP
   and 60 + 12 * 15 = 240 HP with 3 * 15 = 45 MP.  The guide's 己方 line for
   this chapter is LV14 技師布蘭多 HP167 MP39 and LV16 機兵蓋亞 HP240 MP45, to
   the number.  Its AP and DP are the same records with their equipment added:
   34 + 4 * 14 = 90 plus 電光砲's 80 is the guide's AP170, 8 + 3 * 14 = 50 plus
   浸漬皮甲's 45 is its DP95, 0 + 7 * 16 = 112 plus 力量拳套's 130 is its AP242
   and 0 + 5 * 16 = 80 plus 硬鐵裝甲's 50 is its DP130 (assets/items.md).

   Chapter 8's 加入 was a MAP07.DAT deployment record wearing the same
   character's name; MAP08.DAT carries neither of these two at all. */
#define CH09_TECHNICIAN_CHARACTER 8
#define CH09_MACHINE_SOLDIER_CHARACTER 9

/* Chapter 9's opening cut-scene, the string at 0x61864 loaded into EAX at
   000210d5 and pushed as fdps_icon_script_run's only argument.  The number in
   the name is the 0-based chapter id and not a script id of its own, which is
   why this is Icon08.dat where fdps_chapter_08_init above holds Icon07.dat at
   the same position.

   As with every member name, the interpreter names IconAni.vfs itself and
   upper-cases this string in place before the container compare (vfs.h,
   rebuild_info/pitfalls.md), so the literal is folded to ICON08.DAT by the
   call and cannot live in read-only storage. */
#define CH09_OPENING_SCRIPT "Icon08.dat"

/* Which unit the cursor is left on: PUSH 0x0 at 000210e8. */
#define CH09_CURSOR_UNIT 0

/* 000210b0.  Six calls, straight line, no branch, no loop and no local -- the
   shape of fdps_chapter_08_init above with a SECOND fdps_roster_add_character
   in front of it, and nothing else changed.  It is the only handler in the
   file that adds two.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 000210b0..000210b5 -- over SUB ESP,0x0 at 000210b6, a zero-byte
   local area written as the six-byte immediate form.  Nothing is addressed off
   EBP anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 000210f2..000210f5
   and the RET at 000210f6 is bare.

   CALLING CONVENTION.  Every argument goes on the stack and the caller takes
   it back: PUSH 0x8 / CALL 0x00023bc0 / ADD ESP,0x4 at 000210bc..000210c3,
   PUSH 0x9 / CALL 0x00023bc0 / ADD ESP,0x4 at 000210c6..000210cd,
   MOV EAX,0x61864 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   000210d5..000210e0, and PUSH 0x0 / CALL 0x0002da50 / ADD ESP,0x4 at
   000210e8..000210ef.  The two argument-less calls at 000210d0 and 000210e3
   are bare CALLs with no push and no adjustment.  Nothing reads [EBP+8] or
   above, so this function takes nothing itself, and EAX is never set after the
   fourth call, so it returns nothing -- the dispatcher reaches it through the
   ninth slot of the table at 00060074 (the dword at 00060094 is 000210b0) and
   ignores EAX.

   VALUES USED AFTER A CALL: none at all.  All six callees return void and
   nothing here reads EAX after any of them; the only register written between
   the prologue and the RET is the EAX that carries the script name literal
   into the fourth call.

   IN CHAPTER 9 THE ADDS MUST PRECEDE THE CUT-SCENE, AND THAT IS OBSERVABLE.
   MAP08.DAT declares EIGHT player slots -- byte +1 of the block
   (src/rsrc.c) -- and the party is six members strong when the chapter opens:
   chapters 1 to 4 add one each, chapters 5 and 6 add nobody, chapter 7 adds
   裘娜 and chapter 8 adds 費塔加.  The two adds here take it to eight, which
   is exactly what the map asks for.  fdps_build_map_unit_array fills player
   slot i from roster slot i only while i is below the roster count and zeroes
   the slot with UNIT_FLAG_RETIRED set otherwise (src/deploy.c).  The array
   this handler's own reset builds is not the one the battle is fought on:
   ICON08.DAT's first SWITCH_MAP, 0x36 at script offset 4, discards it, and
   its last, SWITCH_MAP 0x08 at offset 421 (0x1a5), calls
   fdps_chapter_state_reset again and rebuilds map 8 out of the roster.  So the
   adds could move behind the reset and slots 6 and 7 would still be 布蘭多
   and 蓋亞; adds that ran after fdps_icon_script_run would leave slots 6 and 7
   as retired blanks and neither of them on the map -- in a chapter whose
   losing condition is either of them dying.  Chapters 4, 7 and 8 all put a member on
   the roster one slot past the last the map wanted, which is why the same
   ordering was invisible there.

   AND THE ORDER OF THE TWO ADDS IS OBSERVABLE, WHICH IS TRUE NOWHERE ELSE IN
   THE FILE.  A party slot is put on placement record
   data_fdps_map_char_spawn_count + slot_index of MAP08.COD (src/deploy.c), so
   roster slot 6 opens on record 37, tile (23, 12), and roster slot 7 on record
   38, tile (24, 13).  Running PUSH 0x9 first would open the chapter with 蓋亞
   on (23, 12) and 布蘭多 on (24, 13) -- the two of them swapped, on the far
   right of the map away from the rest of the party, where the strategy guide's
   whole account of the chapter is 布蘭多 sheltering behind 蓋亞.

   THE CUT-SCENE SPENDS ITSELF ON TWO MAPS OF ITS OWN AND PUTS THIS ONE BACK.
   Walked with the opcode ladder in src/icon.c, the shipped ICON08.DAT's 885
   bytes hold three SWITCH_MAPs and one DEPLOY_WAVE: SWITCH_MAP 0x36 at script
   offset 4 rebuilds on MAP54.DAT, a cut-scene map with no player slot whose
   sixteen wave-0 actors are the party and nine scene-only characters all at
   level 2; SWITCH_MAP 0x37 at offset 169 rebuilds on MAP55.DAT, three actors
   of which 布蘭多 is wave 0 and 蓋亞 wave 1; DEPLOY_WAVE 1 with the
   place-exact operand 1 at offset 295 is what brings that 蓋亞 stand-in on;
   and SWITCH_MAP 0x08 at offset 421 sets the chapter id back to 8 and rebuilds
   map 8 from scratch, with no DEPLOY_WAVE after it.

   So the array the handler returns with is the one that last switch built:
   eight player slots and the twenty-four records MAP08.DAT tags wave 0, for
   thirty-two.  Those twenty-four are the strategy guide's opening 敵方 list to
   the number -- one LV14 魔導士, three LV14 冰魔導士, five LV13 暗黑騎兵, five
   LV13 騎兵, six LV13 傭兵L and four LV13 弓兵 -- and the six records the map
   tags wave 1 are its 援軍 line, which no opcode here deploys: the chapter's
   own turn-15 event is what brings them on.  MAP08.COD carries thirty-nine
   placement records behind its nine-byte header, thirty-one scripted and eight
   for the party, which is the same division from the other side.

   NOTHING IS WRITTEN ON A UNIT.  The body has no store in it at all, as
   chapters 2 to 8 have none, and the cut-scene adds nothing either: walked
   with the same ladder the shipped ICON08.DAT holds no SET_UNIT_TIMER
   anywhere.  The guide lists no status on anybody this chapter.

   THE CHAPTER IS NOT SET HERE.  Both the script the fourth call loads and the
   title-card graphic the fifth one shows are chosen from
   data_fdps_chapter_current_chapter_id by the callees, and this handler
   neither reads nor writes it -- the dispatcher that reached this slot is what
   put the right value there, and the cut-scene's own run of switches leaves it
   as it found it.  The Icon08.dat above is the one place the chapter number is
   spelled out rather than read. */
void fdps_chapter_09_init(void)
{
    fdps_roster_add_character(CH09_TECHNICIAN_CHARACTER);
    fdps_roster_add_character(CH09_MACHINE_SOLDIER_CHARACTER);
    fdps_chapter_state_reset();
    fdps_icon_script_run(CH09_OPENING_SCRIPT);
    fdps_show_chapter_title_card();
    fdps_map_cursor_move_to_unit(CH09_CURSOR_UNIT);
}

/* --- fdps_chapter_10_init @ 00021100 ----------------------------------- */

/* Chapter 10's opening cut-scene, the string at 0x61870 loaded into EAX at
   00021111 and pushed as fdps_icon_script_run's only argument.  The number in
   the name is the 0-based chapter id and not a script id of its own, which is
   why this is Icon09.dat where fdps_chapter_09_init above holds Icon08.dat at
   the same position.

   As with every member name, the interpreter names IconAni.vfs itself and
   upper-cases this string in place before the container compare (vfs.h,
   rebuild_info/pitfalls.md), so the literal is folded to ICON09.DAT by the
   call and cannot live in read-only storage. */
#define CH10_OPENING_SCRIPT "Icon09.dat"

/* Which unit the cursor is left on: PUSH 0x0 at 00021124. */
#define CH10_CURSOR_UNIT 0

/* 00021100.  Four calls, straight line, no branch, no loop and no local -- the
   plain form of the family, the same four calls in the same order as
   fdps_chapter_05_init and fdps_chapter_06_init, with no
   fdps_roster_add_character in front of them and no store behind them.

   The frame is the standard four-push Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV
   EBP,ESP at 00021100..00021104 -- over SUB ESP,0x0 at 00021106, a zero-byte
   local area written as the six-byte immediate form.  Nothing is addressed off
   EBP anywhere in the body, so there is no local here to name and none is
   declared; the four registers come back off the stack at 0002112e..00021131
   and the RET at 00021132 is bare.

   CALLING CONVENTION.  Every argument goes on the stack and the caller takes
   it back: MOV EAX,0x61870 / PUSH EAX / CALL 0x00021650 / ADD ESP,0x4 at
   00021111..0002111c, and PUSH 0x0 / CALL 0x0002da50 / ADD ESP,0x4 at
   00021124..0002112b.  The two argument-less calls at 0002110c and 0002111f
   are bare CALLs with no push and no adjustment.  Nothing reads [EBP+8] or
   above, so this function takes nothing itself, and EAX is never set after the
   third call, so it returns nothing -- the dispatcher reaches it through the
   tenth slot of the table at 00060074 (the dword at 00060098 is 00021100) and
   ignores EAX.

   VALUES USED AFTER A CALL: none at all.  All four callees return void and
   nothing here reads EAX after any of them; the only register written between
   the prologue and the RET is the EAX that carries the script name literal
   into the second call.

   NOBODY JOINS THE PARTY THIS CHAPTER, AND NO SLOT IS LEFT OVER.  The first
   instruction after the prologue is the state rebuild, so the roster is left
   exactly as chapter 9 finished it: 蘭迪斯, 尤利安, 亞克, 法蓮娜, 裘娜,
   費塔加, 布蘭多 and 蓋亞, eight members, chapters 1 to 4 having added one
   each and chapters 7, 8 and 9 the other four.  MAP09.DAT declares EIGHT
   player slots -- byte +1 of the block (src/rsrc.c) -- so every slot has a
   member behind it and fdps_build_map_unit_array writes no zeroed, retired
   spare anywhere in the array (src/deploy.c).  The strategy guide's account of
   the chapter is written around 己方八位人員, which is the same eight from the
   other side.

   THE CUT-SCENE SPENDS ITSELF ON TWO MAPS OF ITS OWN AND PUTS THIS ONE BACK,
   AND DEPLOYS NOTHING ANYWHERE.  Walked with the opcode ladder in src/icon.c,
   the shipped ICON09.DAT's 854 bytes hold three SWITCH_MAPs and no DEPLOY_WAVE
   at all: SWITCH_MAP 0x25 at script offset 4 rebuilds on MAP37.DAT and
   SWITCH_MAP 0x26 at offset 227 on MAP38.DAT -- cut-scene maps whose actors
   are level-2 stand-ins, twelve and eleven of them, all tagged wave 0 -- and
   SWITCH_MAP 0x09 at offset 701 sets the chapter id back to 9 and rebuilds map
   9 from scratch.

   So the array the handler returns with is the one that last switch built:
   eight player slots and the eight records MAP09.DAT tags wave 0, for sixteen.
   Those eight are the strategy guide's opening 敵方 group to the number --
   four LV13 步兵, two LV13 騎兵 and two LV15 魔導士 -- and the forty records
   the map tags waves 1 to 11 are the reinforcement lines the guide lists
   against turns 3, 6 to 13 and 19, which no opcode here deploys.

   NOTHING IS WRITTEN ON A UNIT.  The body has no store in it at all, as
   chapters 2 to 9 have none, and the cut-scene adds nothing either: walked
   with the same ladder the shipped ICON09.DAT holds no SET_UNIT_TIMER
   anywhere.  The guide lists no status on anybody this chapter.

   THE CHAPTER IS NOT SET HERE.  Both the script the second call loads and the
   title-card graphic the third one shows are chosen from
   data_fdps_chapter_current_chapter_id by the callees, and this handler
   neither reads nor writes it -- the dispatcher that reached this slot is what
   put the right value there, and the cut-scene's own run of switches leaves it
   as it found it.  The Icon09.dat above is the one place the chapter number is
   spelled out rather than read. */
void fdps_chapter_10_init(void)
{
    fdps_chapter_state_reset();
    fdps_icon_script_run(CH10_OPENING_SCRIPT);
    fdps_show_chapter_title_card();
    fdps_map_cursor_move_to_unit(CH10_CURSOR_UNIT);
}
