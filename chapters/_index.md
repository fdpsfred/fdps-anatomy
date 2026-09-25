# chapters — 關卡視角

回答「每一章的關卡內容與事件流程是什麼」。遊戲的關卡單位是章，共 30 章，沒有序章或終章（依據：攻略站「遊戲攻略」頁，鏡像見 [`docs/guide/`](../docs/guide/_index.md)）。一章一檔 `chNN.md`（章號補零兩位），每章檔寫該章專屬的內容：劇情、加入角色、敵人與波次、寶物、勝敗條件、處理流程、回合與格子事件、過場腳本與全部對白。

章號 1 起算，`FDPS.LE` 內部的章節索引 0 起算，兩者差 1（見 [`CONTEXT.md`](../CONTEXT.md) 的詞條）。下列各表併列兩種編號，從程式側的常數表取值再寫進章節檔時，以此換算。

本索引是**跨章事實的唯一擁有者**：四張章節處理表的逐章總表與共用處理函式、跨章機制鏈、章號與資源檔的對照、額外場景地圖的歸屬、村莊與連戰區段都只寫在這裡，章節檔不重複。處理表怎麼被呼叫、回合事件與格子事件怎麼觸發是機制，屬於 `program_info/`；各章頁與本索引只寫內容。

章節裡到不了的內容——永遠不部署的波次、永遠不顯示的對白、額外場景裡的殘留——由 [`cut_content/story.md`](../cut_content/story.md) 擁有，各章頁只寫一行連過去；沒有格子引用的寶物記錄由 [`cut_content/items.md`](../cut_content/items.md) 擁有。

每章頁裡夾在 `<!-- chapter_docs:… -->` 標記之間的區塊（含 `maps/` 的戰場全圖）與本頁的表，由 [`tools/chapter_docs/`](../tools/chapter_docs/_index.md) 從出貨資料與 `src/` 產生，不直接編輯。

## 章節總表

<!-- chapter_docs:index_chapters -->
| 章號 | 章節索引 | 章名 | 勝利條件 | 敗北條件 | 文件 |
| ---: | ---: | --- | --- | --- | --- |
| 1 | 0 | 英雄之出發 | 敵人全滅 | 蘭迪斯死亡／索爾死亡 | [`ch01.md`](ch01.md) |
| 2 | 1 | 惡魔之窟 | 敵人全滅 | 蘭迪斯死亡／索爾死亡 | [`ch02.md`](ch02.md) |
| 3 | 2 | 石巨神之封印 | 廿二回合內打倒魔導士 | 蘭迪斯死亡 | [`ch03.md`](ch03.md) |
| 4 | 3 | 謎之女 | 敵人全滅 | 蘭迪斯﹑法蓮娜﹑索爾／任一人死亡 | [`ch04.md`](ch04.md) |
| 5 | 4 | 疾風之帝國軍 | 敵人全滅 | 蘭迪斯﹑法蓮娜﹑索爾／任一人死亡 | [`ch05.md`](ch05.md) |
| 6 | 5 | 帝國軍追擊 | 敵人全滅 | 蘭迪斯﹑法蓮娜﹑索爾／任一人死亡 | [`ch06.md`](ch06.md) |
| 7 | 6 | 競技場戰士 | 敵人全滅 | 蘭迪斯死亡 | [`ch07.md`](ch07.md) |
| 8 | 7 | 地底監獄 | 村民脫離戰場 | 蘭迪斯或費塔加死亡／村民全滅 | [`ch08.md`](ch08.md) |
| 9 | 8 | 布蘭多與蓋亞 | 敵人全滅 | 蘭迪斯　布蘭多或蓋亞／其中一人死亡 | [`ch09.md`](ch09.md) |
| 10 | 9 | 宗教法庭 | 戰場底部脫離 | 蘭迪斯死亡 | [`ch10.md`](ch10.md) |
| 11 | 10 | 琴琴參見 | 敵人全滅 | 蘭迪斯或琴琴死亡 | [`ch11.md`](ch11.md) |
| 12 | 11 | 火神的宮殿 | 敵人全滅 | 蘭迪斯死亡 | [`ch12.md`](ch12.md) |
| 13 | 12 | 地獄三鬥神 | 敵人全滅 | 蘭迪斯死亡 | [`ch13.md`](ch13.md) |
| 14 | 13 | 天空之騎士 | 敵人全滅 | 蘭迪斯死亡／法蓮娜死亡 | [`ch14.md`](ch14.md) |
| 15 | 14 | 要塞砲危機 | 摧毀要塞砲 | 蘭迪斯或瑪麗安死亡 | [`ch15.md`](ch15.md) |
| 16 | 15 | 羅特帝亞突入 | 敵人全滅 | 蘭迪斯死亡 | [`ch16.md`](ch16.md) |
| 17 | 16 | 人質之危機 | 敵人全滅 | 法蓮娜死亡 | [`ch17.md`](ch17.md) |
| 18 | 17 | 咆哮之獅王 | 敵人全滅 | 蘭迪斯死亡 | [`ch18.md`](ch18.md) |
| 19 | 18 | 蛇口之道 | 敵人全滅 | 蘭迪斯死亡 | [`ch19.md`](ch19.md) |
| 20 | 19 | 迷走之隧道 | 敵人全滅 | 蘭迪斯死亡／法蓮娜死亡 | [`ch20.md`](ch20.md) |
| 21 | 20 | 地底神殿 | 敵人全滅 | 蘭迪斯死亡 | [`ch21.md`](ch21.md) |
| 22 | 21 | 巫湯婆婆 | 擊倒巫湯婆婆 | 法蓮娜死亡 | [`ch22.md`](ch22.md) |
| 23 | 22 | 死神冥河 | 擊倒死神 | 法蓮娜死亡／蘭迪斯從戰場上方消失 | [`ch23.md`](ch23.md) |
| 24 | 23 | 魔精石之秘密 | 敵人全滅 | 蘭迪斯死亡 | [`ch24.md`](ch24.md) |
| 25 | 24 | 魔戰將軍 | 魔戰將軍死亡 | 蘭迪斯死亡 | [`ch25.md`](ch25.md) |
| 26 | 25 | 狂信人之塔 | 擊倒魔戰將軍 | 蘭迪斯死亡　索爾死亡 | [`ch26.md`](ch26.md) |
| 27 | 26 | 魔導士的野望 | 魔導王死亡 | 蘭迪斯死亡 | [`ch27.md`](ch27.md) |
| 28 | 27 | 異界之封印 | 敵人全滅 | 蘭迪斯死亡 | [`ch28.md`](ch28.md) |
| 29 | 28 | 守護魔龍 | 敵人全滅 | 蘭迪斯死亡 | [`ch29.md`](ch29.md) |
| 30 | 29 | 最終聖戰 | 擊倒平衡之神 | 蘭迪斯死亡 | [`ch30.md`](ch30.md) |
<!-- /chapter_docs:index_chapters -->

## 章節處理表

<!-- chapter_docs:index_handlers -->
進入處理 `0x60074`、行動後檢查 `0x6028c`、勝利處理 `0x60304` 都是 30 格、以章節索引索引，每章各有自己的一支，沒有兩章共用同一支。

| 章號 | 進入處理 | 行動後檢查 | 勝利處理 |
| ---: | --- | --- | --- |
| 1 | `fdps_chapter_01_init`（`0x20e90`） | `fdps_chapter_01_post_action`（`0x3a3b0`） | `fdps_chapter_01_end`（`0x3a410`） |
| 2 | `fdps_chapter_02_init`（`0x20ef0`） | `fdps_chapter_02_post_action`（`0x3a450`） | `fdps_chapter_02_end`（`0x3a470`） |
| 3 | `fdps_chapter_03_init`（`0x20f30`） | `fdps_chapter_03_post_action`（`0x3a4b0`） | `fdps_chapter_03_end`（`0x3a520`） |
| 4 | `fdps_chapter_04_init`（`0x20f70`） | `fdps_chapter_04_post_action`（`0x3a560`） | `fdps_chapter_04_end`（`0x3a590`） |
| 5 | `fdps_chapter_05_init`（`0x20fb0`） | `fdps_chapter_05_post_action`（`0x3a5d0`） | `fdps_chapter_05_end`（`0x3a600`） |
| 6 | `fdps_chapter_06_init`（`0x20ff0`） | `fdps_chapter_06_post_action`（`0x3a640`） | `fdps_chapter_06_end`（`0x3a670`） |
| 7 | `fdps_chapter_07_init`（`0x21030`） | `fdps_chapter_07_post_action`（`0x3a6b0`） | `fdps_chapter_07_end`（`0x3a6d0`） |
| 8 | `fdps_chapter_08_init`（`0x21070`） | `fdps_chapter_08_post_action`（`0x3a710`） | `fdps_chapter_08_end`（`0x3a800`） |
| 9 | `fdps_chapter_09_init`（`0x210b0`） | `fdps_chapter_09_post_action`（`0x3a840`） | `fdps_chapter_09_end`（`0x3a880`） |
| 10 | `fdps_chapter_10_init`（`0x21100`） | `fdps_chapter_10_post_action`（`0x3a8c0`） | `fdps_chapter_10_end`（`0x3a960`） |
| 11 | `fdps_chapter_11_init`（`0x21140`） | `fdps_chapter_11_post_action`（`0x3a9a0`） | `fdps_chapter_11_end`（`0x3a9d0`） |
| 12 | `fdps_chapter_12_init`（`0x21180`） | `fdps_chapter_12_post_action`（`0x3aa10`） | `fdps_chapter_12_end`（`0x3aa30`） |
| 13 | `fdps_chapter_13_init`（`0x211c0`） | `fdps_chapter_13_post_action`（`0x3aa70`） | `fdps_chapter_13_end`（`0x3aa90`） |
| 14 | `fdps_chapter_14_init`（`0x21200`） | `fdps_chapter_14_post_action`（`0x3aad0`） | `fdps_chapter_14_end`（`0x3aaf0`） |
| 15 | `fdps_chapter_15_init`（`0x21240`） | `fdps_chapter_15_post_action`（`0x3ab30`） | `fdps_chapter_15_end`（`0x3ac20`） |
| 16 | `fdps_chapter_16_init`（`0x21290`） | `fdps_chapter_16_post_action`（`0x3acb0`） | `fdps_chapter_16_end`（`0x3acd0`） |
| 17 | `fdps_chapter_17_init`（`0x212d0`） | `fdps_chapter_17_post_action`（`0x3ad10`） | `fdps_chapter_17_end`（`0x3ad40`） |
| 18 | `fdps_chapter_18_init`（`0x21310`） | `fdps_chapter_18_post_action`（`0x3ad80`） | `fdps_chapter_18_end`（`0x3ada0`） |
| 19 | `fdps_chapter_19_init`（`0x21350`） | `fdps_chapter_19_post_action`（`0x3ae80`） | `fdps_chapter_19_end`（`0x3b0b0`） |
| 20 | `fdps_chapter_20_init`（`0x21390`） | `fdps_chapter_20_post_action`（`0x3b150`） | `fdps_chapter_20_end`（`0x3b1d0`） |
| 21 | `fdps_chapter_21_init`（`0x213d0`） | `fdps_chapter_21_post_action`（`0x3b210`） | `fdps_chapter_21_end`（`0x3b230`） |
| 22 | `fdps_chapter_22_init`（`0x21410`） | `fdps_chapter_22_post_action`（`0x3b270`） | `fdps_chapter_22_end`（`0x3b2a0`） |
| 23 | `fdps_chapter_23_init`（`0x21450`） | `fdps_chapter_23_post_action`（`0x3b2e0`） | `fdps_chapter_23_end`（`0x3b350`） |
| 24 | `fdps_chapter_24_init`（`0x21490`） | `fdps_chapter_24_post_action`（`0x3b3d0`） | `fdps_chapter_24_end`（`0x3b600`） |
| 25 | `fdps_chapter_25_init`（`0x214d0`） | `fdps_chapter_25_post_action`（`0x3b6b0`） | `fdps_chapter_25_end`（`0x3b720`） |
| 26 | `fdps_chapter_26_init`（`0x21510`） | `fdps_chapter_26_post_action`（`0x3b760`） | `fdps_chapter_26_end`（`0x3b800`） |
| 27 | `fdps_chapter_27_init`（`0x21550`） | `fdps_chapter_27_post_action`（`0x3b8a0`） | `fdps_chapter_27_end`（`0x3b8f0`） |
| 28 | `fdps_chapter_28_init`（`0x21590`） | `fdps_chapter_28_post_action`（`0x3b990`） | `fdps_chapter_28_end`（`0x3b9b0`） |
| 29 | `fdps_chapter_29_init`（`0x215d0`） | `fdps_chapter_29_post_action`（`0x3b9f0`） | `fdps_chapter_29_end`（`0x3ba10`） |
| 30 | `fdps_chapter_30_init`（`0x21610`） | `fdps_chapter_30_post_action`（`0x3ba50`） | `fdps_chapter_30_end`（`0x3ba80`） |
<!-- /chapter_docs:index_handlers -->

## 章節事件處理表與共用處理函式

<!-- chapter_docs:index_event_slots -->
章節事件處理表 `0x601c4` 有 50 格，以地圖資料裡的 slot 編號索引（回合事件、格子事件、種類 2 以上的可搜尋格記錄、死亡腳本 opcode 2）。下表是每個 slot 的處理函式與出貨資料裡呼叫它的章。

| slot | 處理函式 | 呼叫它的章（來源） |
| ---: | --- | --- |
| 0 | `fdps_chapter_03_event_deploy_wave_for_turn`（`0x36bb0`） | [第 3 章](ch03.md)（回合事件） |
| 1 | `fdps_chapter_03_event_deploy_wave_1`（`0x36c70`） | [第 3 章](ch03.md)（格子事件） |
| 2 | `fdps_chapter_event_set_game_over`（`0x36cd0`） | 出貨資料沒有任何一筆呼叫這個 slot，見 [刪減與未用](../cut_content/_index.md) |
| 3 | `fdps_chapter_02_event_enemies_advance`（`0x36d00`） | [第 2 章](ch02.md)（回合事件） |
| 4 | `fdps_chapter_03_event_deploy_wave_14`（`0x36ea0`） | [第 3 章](ch03.md)（格子事件） |
| 5 | `fdps_chapter_03_event_turn_limit_game_over`（`0x36f10`） | [第 3 章](ch03.md)（回合事件） |
| 6 | `fdps_chapter_04_event_for_turn`（`0x36f70`） | [第 4 章](ch04.md)（回合事件） |
| 7 | `fdps_chapter_05_event_enemies_advance`（`0x370e0`） | [第 5 章](ch05.md)（格子事件） |
| 8 | `fdps_chapter_06_event_deploy_wave_2`（`0x37170`） | [第 6 章](ch06.md)（死亡腳本） |
| 9 | `fdps_chapter_07_event_enemies_advance`（`0x37250`） | [第 7 章](ch07.md)（回合事件） |
| 10 | `fdps_chapter_08_event_for_turn`（`0x372d0`） | [第 8 章](ch08.md)（回合事件） |
| 11 | `fdps_chapter_08_event_send_guest_mage_to_cells`（`0x37440`） | [第 8 章](ch08.md)（死亡腳本） |
| 12 | `fdps_chapter_08_event_villagers_leave_cells`（`0x374e0`） | [第 8 章](ch08.md)（格子事件） |
| 13 | `fdps_chapter_08_event_villager_escapes`（`0x37600`） | [第 8 章](ch08.md)（格子事件） |
| 14 | `fdps_chapter_09_event_deploy_wave_1`（`0x37730`） | [第 9 章](ch09.md)（回合事件） |
| 15 | `fdps_chapter_10_event_deploy_wave_for_turn`（`0x37780`） | [第 10 章](ch10.md)（回合事件） |
| 16 | `fdps_chapter_10_event_deploy_wave_10`（`0x378a0`） | [第 10 章](ch10.md)（格子事件） |
| 17 | `fdps_chapter_11_event_deploy_wave_for_turn`（`0x378f0`） | [第 11 章](ch11.md)（回合事件） |
| 18 | `fdps_chapter_13_event_enemies_advance`（`0x379d0`） | [第 13 章](ch13.md)（死亡腳本） |
| 19 | `fdps_chapter_14_event_deploy_wave_4`（`0x37a50`） | [第 14 章](ch14.md)（回合事件） |
| 20 | `fdps_chapter_15_event_activate_enemy_group`（`0x37af0`） | [第 15 章](ch15.md)（死亡腳本） |
| 21 | `fdps_chapter_15_event_boss_defeat`（`0x37b70`） | [第 15 章](ch15.md)（死亡腳本） |
| 22 | `fdps_chapter_16_event_wandering_smith_forge`（`0x37cd0`） | [第 16 章](ch16.md)（格子事件） |
| 23 | `fdps_chapter_16_event_enemies_advance_for_turn`（`0x38020`） | [第 16 章](ch16.md)（回合事件） |
| 24 | `fdps_chapter_17_event_deploy_wave_for_turn`（`0x38110`） | [第 17 章](ch17.md)（回合事件） |
| 25 | `fdps_chapter_18_event_deploy_wave_for_turn`（`0x38150`） | [第 18 章](ch18.md)（回合事件） |
| 26 | `fdps_chapter_19_event_lancelot_joins`（`0x38180`） | [第 19 章](ch19.md)（回合事件） |
| 27 | `fdps_chapter_19_event_deploy_wave_6`（`0x381d0`） | [第 19 章](ch19.md)（格子事件） |
| 28 | `fdps_chapter_20_event_upgrade_randis_sword`（`0x382f0`） | [第 20 章](ch20.md)（格子事件） |
| 29 | `fdps_chapter_21_event_deploy_wave_1`（`0x38380`） | [第 21 章](ch21.md)（格子事件） |
| 30 | `fdps_chapter_21_event_deploy_wave_2`（`0x38400`） | [第 21 章](ch21.md)（格子事件） |
| 31 | `fdps_chapter_22_event_for_turn`（`0x384e0`） | [第 22 章](ch22.md)（回合事件） |
| 32 | `fdps_chapter_22_event_boss_defeat`（`0x388b0`） | [第 22 章](ch22.md)（死亡腳本） |
| 33 | `fdps_chapter_23_event_boss_defeat`（`0x38950`） | [第 23 章](ch23.md)（死亡腳本） |
| 34 | `fdps_chapter_23_event_deploy_wave_for_turn`（`0x38a30`） | [第 23 章](ch23.md)（回合事件） |
| 35 | `fdps_chapter_23_event_give_martial_artist_ring`（`0x38b60`） | [第 23 章](ch23.md)（格子事件） |
| 36 | `fdps_chapter_24_event_deploy_wave_for_turn`（`0x38cc0`） | [第 24 章](ch24.md)（回合事件） |
| 37 | `fdps_chapter_25_event_deploy_wave_1`（`0x38e60`） | [第 25 章](ch25.md)（格子事件） |
| 38 | `fdps_chapter_25_event_upgrade_randis_sword`（`0x38fd0`） | [第 25 章](ch25.md)（格子事件） |
| 39 | `fdps_chapter_25_event_marian_buys_wind_god_bow`（`0x39060`） | [第 25 章](ch25.md)（格子事件） |
| 40 | `fdps_chapter_26_event_enemies_advance`（`0x39190`） | [第 26 章](ch26.md)（回合事件） |
| 41 | `fdps_chapter_26_event_deploy_waves_2_and_3`（`0x39230`） | [第 26 章](ch26.md)（格子事件） |
| 42 | `fdps_chapter_26_event_wave_2_defeated_line`（`0x393b0`） | [第 26 章](ch26.md)（死亡腳本） |
| 43 | `fdps_chapter_27_event_deploy_wave_1`（`0x39440`） | [第 27 章](ch27.md)（死亡腳本） |
| 44 | `fdps_chapter_28_event_deploy_wave_for_turn`（`0x39550`） | [第 28 章](ch28.md)（回合事件） |
| 45 | `fdps_chapter_29_event_activate_enemy_groups`（`0x395d0`） | [第 29 章](ch29.md)（格子事件） |
| 46 | `fdps_chapter_29_event_activate_all_enemies`（`0x39670`） | [第 29 章](ch29.md)（格子事件） |
| 47 | `fdps_chapter_30_event_deploy_wave_4`（`0x39770`） | [第 30 章](ch30.md)（格子事件） |
| 48 | `fdps_chapter_30_event_deploy_wave_2`（`0x39840`） | [第 30 章](ch30.md)（死亡腳本） |
| 49 | `fdps_chapter_30_event_deploy_wave_3`（`0x398c0`） | [第 30 章](ch30.md)（死亡腳本） |
<!-- /chapter_docs:index_event_slots -->

## 章號與資源檔的對照

<!-- chapter_docs:index_resources -->
| 章號 | 戰場地圖 | 文字區塊 | 開場腳本 | 勝利腳本 | 戰鬥中事件腳本 | 本章之前的村莊 | 光碟 |
| ---: | --- | --- | --- | --- | --- | --- | ---: |
| 1 | `MAP00` | `FDETXT01` | `ICON00.DAT` | `WIN00.DAT` | — | — | 1 |
| 2 | `MAP01` | `FDETXT02` | `ICON01.DAT` | `WIN01.DAT` | — | `SHOP01` | 1 |
| 3 | `MAP02` | `FDETXT03` | `ICON02.DAT` | `WIN02.DAT` | — | `SHOP02` | 1 |
| 4 | `MAP03` | `FDETXT04` | `ICON03.DAT` | `WIN03.DAT` | — | `SHOP03` | 1 |
| 5 | `MAP04` | `FDETXT05` | `ICON04.DAT` | `WIN04.DAT` | — | `SHOP04` | 1 |
| 6 | `MAP05` | `FDETXT06` | `ICON05.DAT` | `WIN05.DAT` | — | `SHOP05` | 1 |
| 7 | `MAP06` | `FDETXT07` | `ICON06.DAT` | `WIN06.DAT` | — | `SHOP06` | 1 |
| 8 | `MAP07` | `FDETXT08` | `ICON07.DAT` | `WIN07.DAT` | `ICON7-1.DAT`、`ICON7-2.DAT`、`ICON7-3.DAT` | `SHOP07` | 1 |
| 9 | `MAP08` | `FDETXT09` | `ICON08.DAT` | `WIN08.DAT` | — | `SHOP08` | 1 |
| 10 | `MAP09` | `FDETXT10` | `ICON09.DAT` | `WIN09.DAT` | — | `SHOP09` | 1 |
| 11 | `MAP10` | `FDETXT11` | `ICON10.DAT` | `WIN10.DAT` | — | `SHOP10` | 1 |
| 12 | `MAP11` | `FDETXT12` | `ICON11.DAT` | `WIN11.DAT` | — | `SHOP11` | 1 |
| 13 | `MAP12` | `FDETXT13` | `ICON12.DAT` | `WIN12.DAT` | — | `SHOP12` | 1 |
| 14 | `MAP13` | `FDETXT14` | `ICON13.DAT` | `WIN13.DAT` | — | `SHOP13` | 1 |
| 15 | `MAP14` | `FDETXT15` | `ICON14.DAT` | `WIN14.DAT` | — | `SHOP14` | 1 |
| 16 | `MAP15` | `FDETXT16` | `ICON15.DAT` | `WIN15.DAT` | — | `SHOP15` | 1 |
| 17 | `MAP16` | `FDETXT17` | `ICON16.DAT` | `WIN16.DAT` | — | — | 1 |
| 18 | `MAP17` | `FDETXT18` | `ICON17.DAT` | `WIN17-1.DAT`、`WIN17.DAT` | — | — | 1 |
| 19 | `MAP18` | `FDETXT19` | `ICON18.DAT` | `WIN18.DAT` | — | `SHOP18` | 2 |
| 20 | `MAP19` | `FDETXT20` | `ICON19.DAT` | `WIN19.DAT` | — | `SHOP19` | 2 |
| 21 | `MAP20` | `FDETXT21` | `ICON20.DAT` | `WIN20.DAT` | — | `SHOP20` | 2 |
| 22 | `MAP21` | `FDETXT22` | `ICON21.DAT` | `WIN21.DAT` | — | — | 2 |
| 23 | `MAP22` | `FDETXT23` | `ICON22.DAT` | `WIN22.DAT` | — | — | 2 |
| 24 | `MAP23` | `FDETXT24` | `ICON23.DAT` | `WIN23.DAT` | — | `SHOP23` | 2 |
| 25 | `MAP24` | `FDETXT25` | `ICON24.DAT` | `WIN24.DAT` | — | `SHOP24` | 2 |
| 26 | `MAP25` | `FDETXT26` | `ICON25.DAT` | `WIN25.DAT` | — | `SHOP25` | 2 |
| 27 | `MAP26` | `FDETXT27` | `ICON26.DAT` | `WIN26.DAT`、`WINGA26.DAT` | — | — | 2 |
| 28 | `MAP27` | `FDETXT28` | `ICON27.DAT` | `WIN27.DAT` | — | — | 2 |
| 29 | `MAP28` | `FDETXT29` | `ICON28.DAT` | `WIN28.DAT` | — | — | 2 |
| 30 | `MAP29` | `FDETXT30` | `ICON29.DAT` | `GOODEND.DAT`、`WIN29.DAT` | — | — | 2 |

地形層（`M%02d.DTL`／`.MPL`／`ATTR`／`DSC`）與戰場地圖同號，見 [`resource_info/terrain.md`](../resource_info/terrain.md)。每章的 CD 音軌由程式內嵌的常數表以章節索引查，見 [`program_info/cd_audio.md`](../program_info/cd_audio.md)。
<!-- /chapter_docs:index_resources -->

## 額外場景地圖的歸屬

<!-- chapter_docs:index_scenes -->
地圖編號 31 以後是不對應章節的額外場景，只由過場腳本的 `SWITCH_MAP` 切過去；切換時一併換掉文字區塊（`FDETXT` 地圖 + 1）與單位。這些區塊的文字由 [`assets/text/scene_text.md`](../assets/text/scene_text.md) 擁有。

| 地圖 | 文字區塊 | 切到它的腳本 |
| ---: | --- | --- |
| 31 | `FDETXT32` | 沒有腳本切過去，見 [刪減與未用](../cut_content/_index.md) |
| 32 | `FDETXT33` | `ICON00.DAT`（第 1 章開場） |
| 33 | `FDETXT34` | 沒有腳本切過去，見 [刪減與未用](../cut_content/_index.md) |
| 34 | `FDETXT35` | `ICON00.DAT`（第 1 章開場） |
| 35 | `FDETXT36` | `ICON00.DAT`（第 1 章開場） |
| 36 | `FDETXT37` | `WIN00.DAT`（第 1 章勝利） |
| 37 | `FDETXT38` | `ICON09.DAT`（第 10 章開場）、`WIN09.DAT`（第 10 章勝利） |
| 38 | `FDETXT39` | `ICON09.DAT`（第 10 章開場） |
| 39 | `FDETXT40` | `WIN03.DAT`（第 4 章勝利） |
| 40 | `FDETXT41` | `ICON11.DAT`（第 12 章開場） |
| 41 | `FDETXT42` | `WIN00.DAT`（第 1 章勝利） |
| 42 | `FDETXT43` | `WIN00.DAT`（第 1 章勝利） |
| 43 | `FDETXT44` | `WIN00.DAT`（第 1 章勝利） |
| 44 | `FDETXT45` | `WIN18.DAT`（第 19 章勝利） |
| 45 | `FDETXT46` | `WIN17-1.DAT`（第 18 章勝利）、`WIN17.DAT`（第 18 章勝利） |
| 46 | `FDETXT47` | `WIN05.DAT`（第 6 章勝利） |
| 47 | `FDETXT48` | `ICON06.DAT`（第 7 章開場）、`WIN24.DAT`（第 25 章勝利） |
| 48 | `FDETXT49` | `ICON07.DAT`（第 8 章開場） |
| 49 | `FDETXT50` | 沒有腳本切過去，見 [刪減與未用](../cut_content/_index.md) |
| 50 | `FDETXT51` | `WIN22.DAT`（第 23 章勝利） |
| 51 | `FDETXT52` | `WIN20.DAT`（第 21 章勝利）、`WIN24.DAT`（第 25 章勝利） |
| 52 | `FDETXT53` | `ICON11.DAT`（第 12 章開場） |
| 53 | `FDETXT54` | `WIN11.DAT`（第 12 章勝利） |
| 54 | `FDETXT55` | `ICON08.DAT`（第 9 章開場） |
| 55 | `FDETXT56` | `ICON08.DAT`（第 9 章開場） |
| 56 | `FDETXT57` | `ICON24.DAT`（第 25 章開場） |
| 57 | `FDETXT58` | `WIN08.DAT`（第 9 章勝利） |
| 58 | `FDETXT59` | `ICON19.DAT`（第 20 章開場） |
| 59 | `FDETXT60` | `WIN24.DAT`（第 25 章勝利） |
| 60 | `FDETXT61` | `WIN16.DAT`（第 17 章勝利） |
| 61 | `FDETXT62` | `WINGA26.DAT`（第 27 章勝利） |
| 62 | `FDETXT63` | `WIN26.DAT`（第 27 章勝利） |
| 63 | `FDETXT64` | `WIN29.DAT`（第 30 章勝利） |
| 64 | `FDETXT65` | `GOODEND.DAT`（第 30 章勝利） |
<!-- /chapter_docs:index_scenes -->

## 村莊與連戰

<!-- chapter_docs:index_villages -->
一章勝利後，章節結束處理把章節索引改成下一章，接著依 `CHAPTER_HAS_NO_VILLAGE`（`src/village.c`）決定要不要進村莊；不進村莊的兩章之間直接到存檔畫面，中間買不到東西，是連戰。

| 兩章之間 | 村莊 |
| --- | --- |
| 第 1 → 2 章 | 有，`SHOP01.DAT` |
| 第 2 → 3 章 | 有，`SHOP02.DAT` |
| 第 3 → 4 章 | 有，`SHOP03.DAT` |
| 第 4 → 5 章 | 有，`SHOP04.DAT` |
| 第 5 → 6 章 | 有，`SHOP05.DAT` |
| 第 6 → 7 章 | 有，`SHOP06.DAT` |
| 第 7 → 8 章 | 有，`SHOP07.DAT` |
| 第 8 → 9 章 | 有，`SHOP08.DAT` |
| 第 9 → 10 章 | 有，`SHOP09.DAT` |
| 第 10 → 11 章 | 有，`SHOP10.DAT` |
| 第 11 → 12 章 | 有，`SHOP11.DAT` |
| 第 12 → 13 章 | 有，`SHOP12.DAT` |
| 第 13 → 14 章 | 有，`SHOP13.DAT` |
| 第 14 → 15 章 | 有，`SHOP14.DAT` |
| 第 15 → 16 章 | 有，`SHOP15.DAT` |
| 第 16 → 17 章 | 無 |
| 第 17 → 18 章 | 無 |
| 第 18 → 19 章 | 有，`SHOP18.DAT` |
| 第 19 → 20 章 | 有，`SHOP19.DAT` |
| 第 20 → 21 章 | 有，`SHOP20.DAT` |
| 第 21 → 22 章 | 無 |
| 第 22 → 23 章 | 無 |
| 第 23 → 24 章 | 有，`SHOP23.DAT` |
| 第 24 → 25 章 | 有，`SHOP24.DAT` |
| 第 25 → 26 章 | 有，`SHOP25.DAT` |
| 第 26 → 27 章 | 無 |
| 第 27 → 28 章 | 無 |
| 第 28 → 29 章 | 無 |
| 第 29 → 30 章 | 無 |

連戰區段（中間沒有村莊的連續章）：第 16–18 章、第 21–23 章、第 26–30 章。
<!-- /chapter_docs:index_villages -->

## 跨章機制鏈

一章打完之後帶進下一章的只有名冊（每人的等級、裝備與背包）、章節索引、隊伍金錢與抽獎旗標，存檔內容見 [`program_info/save.md`](../program_info/save.md)。FDPS 沒有跨章的劇情旗標：下面每一條跨章的鏈，要不是以「某件物品在某人背包裡」接起來，就是以「名冊的入隊順序讓某人固定坐在某個單位索引」接起來。

### 章節之間帶過去的狀態

- **章內旗標不跨章。** 每章的進入處理都呼叫 `fdps_chapter_state_reset`（`0x22750`），它在載入新章的單位之後把 32 格的格子事件已觸發旗標整塊清零。各章的一次性事件、決鬥狀態、伏兵鎖共用元素 `0x10`–`0x12`（例如第 15 章的決鬥用 `0x10`、第 19、24 章的決鬥用 `0x11`、第 26 章與第 27 章都用 `0x12`），彼此不會互相干擾。
- **章節索引是唯一往下傳的進度。** 每章的勝利處理都把章節索引寫成下一章（寫的是常數，不是加一），接下來的村莊與下一章都依它載入；只有第 27 章一般結局與第 30 章不寫，見下面「結局分歧」。哪兩章之間有村莊見上面的「村莊與連戰」表。
- **章節索引本身被當成條件讀。** 共用敗北判定 `fdps_battle_check_default_end_conditions`（`0x3a2e0`）對章節索引 `0x10`（[第 17 章](ch17.md)）與 `0x15`（[第 22 章](ch22.md)）改看單位 3（法蓮娜），其他章看單位 0（蘭迪斯）。第 21 章的 `fdps_chapter_21_end`（`0x3b230`）寫入 `0x15`，所以蘭迪斯在第 21 章勝利過場倒下之後，下一章的敗北條件不再看他；不過第 22 章的 `fdps_chapter_22_post_action`（`0x3b270`）不呼叫共用判定，自己只看單位 3，共用判定的 `0x15` 那一支實際上走不到。
- **村莊選單以章節索引鎖住法蓮娜。** `fdps_village_select_member`（`0x336d0`）在章節索引 `0x17` 以後拒絕確認名冊第 3 格、並把角色編號 1 的記錄畫成半透明；沒有上限，但 `0x17` 以後有村莊的只有第 24、25、26 章之前那三座，所以實際作用在她離隊的期間（見 [第 23 章](ch23.md)）。

### 條件式招募

FDPS 沒有條件式招募。全部 12 名隊員都由該章的進入處理無條件呼叫 `fdps_roster_add_character`（`0x23bc0`）加入名冊，沒有分支，也不看之前章節做過什麼；勝利處理從不加人：

| 入隊章 | 進入處理 | 入隊者（角色編號） |
| ---: | --- | --- |
| [1](ch01.md) | `fdps_chapter_01_init`（`0x20e90`） | 蘭迪斯（`00`） |
| [2](ch02.md) | `fdps_chapter_02_init`（`0x20ef0`） | 尤利安（`06`） |
| [3](ch03.md) | `fdps_chapter_03_init`（`0x20f30`） | 亞克（`04`） |
| [4](ch04.md) | `fdps_chapter_04_init`（`0x20f70`） | 法蓮娜（`01`） |
| [7](ch07.md) | `fdps_chapter_07_init`（`0x21030`） | 裘娜（`03`） |
| [8](ch08.md) | `fdps_chapter_08_init`（`0x21070`） | 費塔加（`02`） |
| [9](ch09.md) | `fdps_chapter_09_init`（`0x210b0`） | 布蘭多（`08`）、蓋亞（`09`），依此順序 |
| [11](ch11.md) | `fdps_chapter_11_init`（`0x21140`） | 琴琴（`07`） |
| [15](ch15.md) | `fdps_chapter_15_init`（`0x21240`） | 瑪麗安（`05`） |
| [19](ch19.md) | `fdps_chapter_19_init`（`0x21350`） | 蘭斯洛特（`0B`） |
| [24](ch24.md) | `fdps_chapter_24_init`（`0x21490`） | 珊（`0A`） |

其餘 19 章的進入處理都不加人。入隊的規則、名冊記錄的來源與各人的加入條件見 [`assets/characters.md` 的「加入」](../assets/characters.md#加入)。

- **名冊記錄會被戰場上的同一角色蓋過。** 勝利時 `fdps_roster_write_back_battle_units`（`0x23980`）以角色編號比對，把戰場單位整筆寫回名冊，所以第 4、8、15、19、24 章由地圖部署記錄（而不是名冊）上場的法蓮娜、費塔加、瑪麗安、蘭斯洛特、珊，之後帶著的是那筆部署記錄的等級與裝備；各章的數值見該章頁。已退場的角色 0（蘭迪斯）是唯一不寫回的記錄，所以開場過場讓他退場的第 22、23 章不會改動他的名冊記錄。
- **索爾（`0C`）從不入隊。** 他在第 1–6 章以友方 NPC 同行，之後在各章出場時也都是該章地圖或過場自己部署的單位，每章的狀態由該章決定，不承接前一章。
- **法蓮娜的歸隊是全遊戲唯一依條件而定的入隊。** 她的名冊記錄在第 23 章勝利後並不刪除，只是第 24–27 章的開場過場都讓單位 3 退場；只有 `fdps_chapter_27_end`（`0x3b8f0`）走隱藏路線時，`WINGA26.DAT` 才讓她復歸、給經驗並寫回名冊，第 28 章起她重新上場。條件見下面「結局分歧」。

### 入隊順序決定的固定單位索引

名冊依入隊順序排列、從不重排，每章建立戰場時由 `fdps_build_map_unit_array`（`0x22be0`）從名冊前端依序放進地圖的我方 slot，所以每位隊員從入隊那章起就固定坐在同一個單位索引。許多處理函式把這個索引寫成常數，靠的就是上表的入隊順序：

- **單位 3 = 法蓮娜**：`fdps_chapter_05_post_action`（`0x3a5d0`）與 `fdps_chapter_06_post_action`（`0x3a640`）以她陣亡判敗北（同形的 `fdps_chapter_04_post_action`（`0x3a560`）在第 4 章指到的是索爾）；第 17、22 章的敗北判定；第 27 章勝利時查反禁制器。
- **單位 4 = 裘娜**：第 15、19、24 章的決鬥只對她提出；`fdps_battle_advance_turn`（`0x1e3f0`）每回合只對單位 4 查裝備中的妖刀村正、妖刀正宗回 MP。
- **單位 6、7 = 布蘭多、蓋亞**：`fdps_chapter_09_post_action`（`0x3a840`）。
- **單位 0–7 = 前八人**：`fdps_chapter_10_post_action`（`0x3a8c0`）以這八格計數脫離者。
- **單位 8 = 琴琴**：`fdps_chapter_11_post_action`（`0x3a9a0`）以她陣亡判敗北；`fdps_battle_advance_turn`（`0x1e3f0`）只對單位 8 查裝備中的形見指環回 MP。
- **單位 0–8 = 前九人**：`fdps_chapter_18_end`（`0x3ada0`）的轉職檢查只看這九格，第 15 章入隊的瑪麗安不在內。

重建時照直覺把這些常數改成「找某角色編號」或「走全部單位」就會與原版不同，見 [`rebuild_info/pitfalls.md`](../rebuild_info/pitfalls.md)。

隊伍在第 16、17 章分兩半輪流出戰，第 22、23 章蘭迪斯不上場，第 24–27 章法蓮娜不上場，都是開場過場的 `RETIRE_UNIT`，不是陣亡：HP 不歸零，勝利時照常寫回名冊，`fdps_roster_revive_fallen_members`（`0x39e70`）不收他們的復活費，下一章建立戰場時退場旗標就清掉了。各章讓誰退場見 [第 16 章](ch16.md)、[第 17 章](ch17.md)、[第 22 章](ch22.md)、[第 23 章](ch23.md)、[第 24 章](ch24.md)。

### 隱藏獎勵

以下獎勵的條件跨越章節，或畫面上沒有提示。每條鏈都靠物品留在某人背包裡、由勝利時的名冊寫回帶到後面的章節；物品效果見 [`assets/items.md`](../assets/items.md)。

**炎龍劍鏈（蘭迪斯）**：

1. [第 12 章](ch12.md)三選一由 `fdps_icon_script_prompt_three_way_choice`（`0x22600`）決定部署哪一隻守護獸，打倒它掉 `58` 修佩魯、`59` 雷德或 `5A` 亞德尼恩。程式不記錄選了哪一間，之後只看物品。
2. [第 16 章](ch16.md)流浪工匠 `fdps_chapter_16_event_wandering_smith_forge`（`0x37cd0`）只看單位 0（蘭迪斯）的背包：修佩魯換成 `A0` 灼烈之劍；雷德折斷，賠 5000 金，再依第二個問題給 `A9` 神的聖印或 `A3` 金屬礦。亞德尼恩沒有任何後續處理，劍在別的隊員身上也不會觸發。
3. [第 20 章](ch20.md) `fdps_chapter_20_event_upgrade_randis_sword`（`0x382f0`）在第 20 回合以內把灼烈之劍換成 `A1` 火光之劍。
4. [第 25 章](ch25.md) `fdps_chapter_25_event_upgrade_randis_sword`（`0x38fd0`）把火光之劍換成 `A2` 真炎龍劍。
5. [第 26 章](ch26.md) `fdps_chapter_26_end`（`0x3b800`）在蘭迪斯沒有真炎龍劍、背包未滿時送 `62` 炎龍劍，是整條鏈斷掉時的替代品。

**勇者徽章（雷德路線）**：第 16 章拿到的神的聖印，在 [第 18 章](ch18.md)勝利時由 `fdps_chapter_18_end`（`0x3ada0`）換成 `DB` 勇者徽章，條件是蘭迪斯仍帶著聖印、且單位 0–8 沒有人轉過職，換到時勝利過場改播 `Win17-1.dat`。教會的 `fdps_church_promote_loop`（`0x345a0`）要尚未轉職的蘭迪斯（造型編號 `00`）帶著勇者徽章才開放路線 3。同一個教會規則下，[第 14 章](ch14.md)寶箱裡的暗之徽章若被狼人搶走就永久失去。

**高能量砲（布蘭多）**：第 16 章雷德路線的另一個答案給 `A3` 金屬礦；[第 24 章](ch24.md)埋藏的 `BB` 高能量裝置由布蘭多使用時，`fdps_apply_item_effect_to_targets`（`0x262a0`）要他背包裡有金屬礦，才把兩件換成 `BE` 高能量砲。

**妖刀鏈（裘娜）**：[第 15 章](ch15.md)決鬥獲勝由 `fdps_chapter_15_post_action`（`0x3ab30`）給 `A5` 妖刀村雨；[第 19 章](ch19.md) `fdps_chapter_19_post_action`（`0x3ae80`）只對帶著村雨的裘娜提出決鬥，贏了換成 `A6` 妖刀村正；[第 24 章](ch24.md) `fdps_chapter_24_post_action`（`0x3b3d0`）在第 25 回合以內、裘娜帶著村正時才提出最後一場，贏了換成 `A7` 妖刀正宗。前一場沒拿到，後面的決鬥就不會出現。另外，第 19 章只要決鬥被提出過（不論接受或拒絕），`fdps_chapter_19_end`（`0x3b0b0`）就免費讓全隊復活並回滿，畫面上沒有提示。

**隱藏路線的兩件物品**：

- [第 22 章](ch22.md)由法蓮娜擊倒巫湯婆婆且背包有空位時，`fdps_chapter_22_event_boss_defeat`（`0x388b0`）給她 `BA` 死神契約；[第 23 章](ch23.md)她帶著契約擊倒死神時，`fdps_chapter_23_event_boss_defeat`（`0x38950`）換成 `DC` 反禁制器。她在第 23 章勝利後就離隊，所以反禁制器只能在這兩章拿到。
- [第 24 章](ch24.md)魔精石底下埋著 `B3` 魔精石碎片。帶著它的單位每回合回 MP（`fdps_battle_advance_turn`（`0x1e3f0`）對所有單位查這一件）。
- 兩件的用途見下面「結局分歧」。

**其他跨章獎勵**：

- [第 1 章](ch01.md) `fdps_chapter_01_end`（`0x3a410`）在名冊寫回之前替蘭迪斯加上法術 `00` 業火，是他取得業火的唯一途徑。
- [第 8 章](ch08.md) `fdps_chapter_08_event_villager_escapes`（`0x37600`）依逃脫人數給的獎勵放進費塔加的背包，隨寫回帶到後面的章節；背包滿時直接消失。
- [第 10 章](ch10.md)第 19 回合出現的 LV40 騎兵，其死亡腳本（由 `fdps_run_death_scripts`（`0x1d990`）執行）給物品 `FF`，之後一直留在背包裡。
- [第 23 章](ch23.md) `fdps_chapter_23_event_give_martial_artist_ring`（`0x38b60`）把 `B1` 形見指環交給琴琴，它的回 MP 只在單位 8 有效（見上一節）。
- [第 7 章](ch07.md)勝利對話說給 1000 金，但 `fdps_chapter_07_end`（`0x3a6d0`）與勝利過場都不動金錢，這筆錢實際上不存在。

### 結局分歧

結局只在 [第 27 章](ch27.md)勝利時決定一次，依據只有兩件物品：`fdps_chapter_27_end`（`0x3b8f0`）先查單位 0（蘭迪斯）背包裡的 `B3` 魔精石碎片與單位 3（法蓮娜）背包裡的 `DC` 反禁制器，兩個查詢都做完才判斷。陣亡人數、第 12 章的選擇、轉職與其他章的結果都不影響分歧。

- **缺任何一件：一般結局。** 播 `WIN26.DAT`，接著 `fdps_play_ending_credit_roll`（`0x1ba40`）播片尾名單與 `End` 影片，然後設起離場旗標回到標題畫面。章節索引留在第 27 章的值，名冊不寫回，兩件物品也不收走。片尾名單讀到章節索引是第 27 章，字幕從 `FDETXT27` 的 `0x19` 起算，並跳過名冊第 3 格（法蓮娜）。
- **兩件都在：隱藏路線。** 收走兩件，播 `WINGA26.DAT`（法蓮娜在過場裡復歸、得到經驗並寫回名冊），付費復活倒下的隊員，章節索引設為 27，進入 [第 28 章](ch28.md)。之後 `fdps_chapter_28_end`（`0x3b9b0`）與 `fdps_chapter_29_end`（`0x3ba10`）照一般形式各進下一章，中間都沒有村莊，直到 [第 30 章](ch30.md)。
- **第 30 章：隱藏結局。** `fdps_chapter_30_end`（`0x3ba80`）沒有分支，也是全遊戲唯一不復活陣亡者、不寫下一章索引的勝利處理：寫回名冊、播 `WIN29.DAT`（最後切回地圖 29，章節索引仍是第 30 章），再呼叫同一支 `fdps_play_ending_credit_roll`（`0x1ba40`）。它讀到第 30 章的索引，字幕從 `FDETXT30` 的 `0x20` 起算，名冊 12 人各一張再加一張索爾的卡片；停 30 秒後播 `GOODEND.DAT`，設起離場旗標結束。

兩個結局用的是同一支片尾名單函式，差別只在它讀到的章節索引；第 27 章以外沒有任何處理函式會播結局。

跨章鏈上沒有任何程式讀到的內容（一般結局裡法蓮娜那一格字幕、第 25 章之後進不去的神秘商店等）見 [刪減與未用](../cut_content/_index.md)。
