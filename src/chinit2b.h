/* chinit2b.h -- the per-chapter entry handlers, chapters 25 to 30.  Chapters 1
 * to 10 are chinit1.h, 11 to 15 are chinit1b.h and 16 to 24 are chinit2.h.
 * This file starts at chapter 25 because chapter 24's handler is the last in
 * the game that adds anybody to the roster.
 *
 * Every entry point here is a slot of the handler table based at 00060074,
 * and the slot number is the 0-based chapter id.  The table is reached
 * indirectly: fdps_title_screen loads data_fdps_chapter_current_chapter_id
 * (gamedata.h), scales it by four and CALLs through the slot with nothing
 * pushed and no stack cleanup, so every handler shares one function-pointer
 * type -- no arguments, no result -- and none of them has a static caller.
 *
 * A handler runs once, at the moment the game enters its chapter and before
 * the battle loop starts.  Its job is to put the chapter's opening situation
 * in place: the rebuilt chapter state, the opening cut-scene and title card,
 * and the tile the map cursor starts on.
 *
 * Nothing here owns state.  The chapter frame and the title card are
 * chapter.h's, the cut-scene interpreter is icon.h's and the map cursor is
 * mapcur.h's.
 */
#ifndef CHINIT2B_H
#define CHINIT2B_H

/* Chapter 25's entry handler: brings the game into chapter 25.  Takes nothing
   and returns nothing.

   In order: the chapter state is rebuilt, the opening cut-scene Icon24.dat is
   interpreted, the chapter title card is shown, and the map cursor is parked
   on unit 0's tile.  Nobody joins the party this chapter -- there is no
   fdps_roster_add_character in the body -- and the roster is left as chapter
   24 finished it, twelve members: the eleven chapters 1 to 19 assembled and
   珊, whom chapter 24's handler appended.  MAP24.DAT is the first map to ask
   for TWELVE player slots, so every slot has a member behind it and no zeroed,
   retired spare is written.

   The cut-scene switches map twice on its own account, which no other member
   in this file does: it resets the state onto map 56, a cut-scene stage with
   nothing of its own on the board, plays the scene there, and resets back onto
   map 24 before it retires anybody.  The chapter id reads 24 again when the
   handler returns because the script put it back, not because nothing touched
   it.

   The chapter is fought without 法蓮娜, and the cut-scene is what takes her
   off: ICON24.DAT retires map unit 3 -- a roster slot, so 法蓮娜 -- with no
   REVIVE behind it, leaving the eleven the strategy guide's 己方 line names,
   法蓮娜以外的所有人.

   The opening opposition is the map's own wave 0 and nothing else; the
   cut-scene carries no DEPLOY_WAVE.  MAP24.DAT tags forty-one of its
   fifty-nine deployment records wave 0, so the board is fifty-three units, and
   those forty-one are the guide's 敵方 list for the chapter less one group:
   LV30 塞克斯, 布魯森 and 汎拉沛 one each (characters 64, 65 and 66), LV16
   黑暗祭司 x1 (104), LV16 地獄騎士 x8 (78), LV16 鎧甲武士 x18 (100) and LV16
   神箭手 x11 (95) -- the three 魔戰將軍 included, so the chapter opens with
   them already on the board.  The remaining eighteen records are wave 1,
   eighteen LV16 天空騎士 (97), the guide's 事件 reinforcement, and this
   handler does not deploy them.

   The cut-scene also places unit 0 and walks it, so the tile the cursor ends
   on is ICON24.DAT's (4, 2) and not MAP24.COD's start tile for player slot 0.

   Table slot 24. */
extern void fdps_chapter_25_init(void);
#pragma aux fdps_chapter_25_init "*" parm caller [];

#endif
