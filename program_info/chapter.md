# 章節生命週期與事件分派

**驗證對象**：`FDPS.LE` 的章節框架——四張章節處理表 `0x60074`（進入）、`0x601c4`（腳本事件）、`0x6028c`（行動後）、`0x60304`（結束）與它們的全部呼叫點；章節進入共用的狀態重置 `0x22750` 與標題卡 `0x20c60`（`src/chapter.c`）；三十支進入處理函式 `0x20e90`–`0x2164f`（`src/chinit1.c`、`chinit1b.c`、`chinit2.c`、`chinit2b.c`）；單位陣列的建立與波次部署 `0x22be0`、`0x232b0`、`0x23830`（`src/deploy.c`）；回合事件的掃描 `0x2e0c0`（`src/btlturn.c`）；五十支腳本事件處理函式 `0x36bb0`–`0x398ed`（`src/chevt1.c`–`chevt6.c`）；共用勝敗判定、殘敵清除與勝敗條件視窗 `0x3a2e0`、`0x39e10`、`0x17ca0`、`0x18350`（`src/btlend.c`）；三十支行動後處理函式與三十支結束處理函式 `0x3a3b0`–`0x3badd`（`src/chpost1.c`–`chpost3.c`、`src/chend1.c`–`chend2b.c`）；章節結束時的名冊寫回 `0x23980` 與陣亡者復活 `0x39e70`（`src/roster.c`）。「一章怎麼開始、戰鬥中的劇情怎麼被觸發、勝敗怎麼判定、一章怎麼收尾」以此檔為唯一正典。`main` 頂層迴圈的形狀由 [`architecture.md`](architecture.md) 擁有，四張表在 `.object2` 的位置由 [`memory_layout.md`](memory_layout.md) 擁有；各章實際的事件內容屬於 [`../chapters/`](../chapters/_index.md)。

本頁的「章節索引」是 0 起算的內部編號（`data_fdps_chapter_current_chapter_id`，`0x69cf4`），章號是它加 1，見 [`../CONTEXT.md`](../CONTEXT.md)。「單位索引」是戰場單位陣列的位置，不是角色編號。

## 一章的流程

`main`（`0x29220`）的頂層迴圈每一圈跑一次戰鬥的玩家階段，再看戰鬥結束碼 `data_fdps_chapter_event_or_battle_end_code`（`0x69da0`）：1（敗北）走 Game Over 回標題畫面，2（過關）跑結束表 `0x60304[章節索引]` 再進村莊階段，其餘值什麼都不做、下一圈繼續同一場戰鬥；迴圈的形狀與分派見 [`architecture.md`](architecture.md) 的「頂層迴圈的形狀」。

一章的完整流程因此是：

1. **進入**：進入表 `0x60074[章節索引]` 的處理函式建立戰場、播開場過場與標題卡（見「進入章節」）。呼叫它的只有兩處：標題畫面的「新遊戲」（`fdps_title_screen` 內的 `0x2a85e`，先把名冊人數與章節索引都設成 0）與村莊階段的結尾（`fdps_run_village_phase` 內的 `0x31505`）。村莊階段本身見[`village.md`](village.md)。
2. **戰鬥**：玩家階段迴圈逐幀處理輸入；回合推進、我方 NPC 與敵方階段見 [`battle.md`](battle.md)。這段期間章節透過兩張表插手：腳本事件表 `0x601c4`（回合事件、格子事件、搜尋、死亡腳本）與行動後表 `0x6028c`（勝敗判定）。
3. **判定**：勝敗由行動後處理函式、腳本事件處理函式與死亡腳本寫進結束碼（見「勝敗判定」）。
4. **結束**：結束碼 2 時跑結束表 `0x60304[章節索引]`：清除殘敵、名冊寫回、勝利過場、陣亡者復活，最後把章節索引改成下一章（見「章節結束」）。接著村莊階段，它的結尾又呼叫進入表，回到第 1 步。

**章節索引由誰改寫。**`0x69cf4` 在映像裡有 34 個寫入點，三十支進入處理函式本身一個都沒有：29 個是 `fdps_chapter_01_end`（`0x3a410`）到 `fdps_chapter_29_end`（`0x3ba10`）在自己最後一步寫入的下一章索引（字面值，不是加 1；第 27 章兩條路線只有隱藏路線寫），其餘 5 個是 `fdps_title_screen` 的新遊戲（0）、`fdps_load_savegame`（`0x23e20`）與 `fdps_load_game_screen`（`0x24490`）從存檔讀出的章節 byte、`fdps_title_demo`（`0x2ac10`）的展示關（`0x19`）、以及過場腳本 `fdps_icon_script_run`（`0x21650`）的 `SWITCH_MAP`（見 [`../resource_info/cutscene_script.md`](../resource_info/cutscene_script.md)）。進入處理函式、標題卡、地圖載入都讀這個全域，所以呼叫進入表之前它必須已經是目標章。

**遊戲的結局是結束程式，不是回到標題畫面。**`fdps_chapter_27_end`（`0x3b8f0`）的一般結局與 `fdps_chapter_30_end`（`0x3ba80`）都以設起離場旗標 `0x643eb` 收尾；`main` 接著呼叫的 `fdps_run_village_phase` 看到旗標就直接回傳（不呼叫進入表），`main` 的迴圈條件不成立，走收尾路徑回到文字模式、印出 `Thank you for playing Flame Dragon Plus!!` 後結束程式。會設起這個旗標的另外只有玩家主動離開遊戲的三處（標題畫面、戰鬥系統選單、酒館）；它唯一的清零是 `main` 啟動時的初始化，所以設起之後不會再回到任何一章。

## 四張處理表

四張表都在 `.object2`，內容是 relocated 函式指標，只以 `CALL dword ptr [reg + 表位址]` 間接呼叫；**沒有任何呼叫點檢查索引範圍**。

| 表 | 筆數 | 索引 | 原型 | 呼叫點 | 呼叫時機 |
| --- | ---: | --- | --- | --- | --- |
| `0x60074` 進入 | 30 | 章節索引 | `void (void)` | `fdps_title_screen` 的 `0x2a85e`、`fdps_run_village_phase` 的 `0x31505` | 新遊戲開始；每次村莊階段結束（離場旗標已設時略過） |
| `0x601c4` 腳本事件 | 50 | 地圖資料給的事件 slot | `void (int unit_index)` | 8 處，見下 | 回合事件、格子事件、搜尋到事件、死亡腳本 opcode 2 |
| `0x6028c` 行動後 | 30 | 章節索引 | `void (void)` | 5 處，見下 | 每個單位行動完、每個陣營階段的狀態結算之後 |
| `0x60304` 結束 | 30 | 章節索引 | `void (void)` | `main` 的 `0x293a1` | 戰鬥以結束碼 2 回到 `main` 時 |

進入、行動後、結束三張表的第 n 筆都是第 n+1 章的函式，依章號排列、沒有空缺也沒有共用（`fdps_chapter_01_init`（`0x20e90`）…`fdps_chapter_30_init`（`0x21610`）、`fdps_chapter_01_post_action`（`0x3a3b0`）…`fdps_chapter_30_post_action`（`0x3ba50`）、`fdps_chapter_01_end`（`0x3a410`）…`fdps_chapter_30_end`（`0x3ba80`））。腳本事件表的 50 筆不依章節分組，地圖資料只以 slot 號碼指它，slot 對照見「戰鬥中的事件分派」。

腳本事件表的 8 個呼叫點全部推一個 dword（單位索引）再自己 `ADD ESP,4`：

| 呼叫點 | 所在 function | 推的單位索引 |
| --- | --- | --- |
| `0x12a33`、`0x12aea` | `fdps_battle_enemy_turn_phase`（`0x12960`）的兩輪 | 剛處理完的敵方單位 |
| `0x12bd6` | `fdps_battle_npc_turn_phase`（`0x12b20`） | 剛處理完的 NPC 單位 |
| `0x14d28` | `fdps_battle_system_menu`（`0x14ab0`）的全軍移動 | 剛移動完的我方單位 |
| `0x15839` | `fdps_battle_unit_turn`（`0x15470`）的結尾 | 行動的我方單位 |
| `0x188d8` | `fdps_battle_search_cell_at_cursor`（`0x184f0`） | 搜尋者 |
| `0x1dcae` | `fdps_run_death_scripts`（`0x1d990`） | 它收到的擊殺者參數，不一定是擊殺者（各情境見 [`battle.md`](battle.md)） |
| `0x2e140` | `fdps_battle_run_turn_events`（`0x2e0c0`） | 固定 0 |

Ghidra 只對其中三個（`0x188d8`、`0x1dcae`、`0x2e140`）建了資料參照，另外五個是 `CALL dword ptr [EDX+0x601c4]` 而沒有參照，所以只查 xref 會漏。

行動後表的 5 個呼叫點不推任何參數：`fdps_battle_enemy_turn_phase` 的 `0x12a48` 與 `0x12aff`、`fdps_battle_npc_turn_phase` 的 `0x12beb`、`fdps_battle_unit_turn` 的 `0x1584e`、`fdps_battle_tick_status_effects`（`0x1fa30`）的 `0x1fb50`。

## 進入章節

三十支進入處理函式都是同一個骨架，依序：

1. `fdps_roster_add_character`（`0x23bc0`）把本章加入的角色接到名冊尾端（第 1、2、3、4、7、8、11、15、19 章各一人，第 9 章兩人，第 24 章一人但排在第 2 步之後）。
2. `fdps_chapter_state_reset`（`0x22750`）。
3. `fdps_icon_script_run`（`0x21650`）播開場過場。檔名是每支處理函式寫死的字面字串 `IconNN.dat`，NN 是該章的**章節索引**（第 1 章是 `Icon00.dat`）；不是由全域組出來的。
4. `fdps_show_chapter_title_card`（`0x20c60`）。
5. `fdps_map_cursor_move_to_unit`（`0x2da50`）把游標放到單位 0；第 17、22、23 章放到單位 3。

例外只有三個：`fdps_chapter_01_init`（`0x20e90`）在第 4 步後把單位 2（開場過場部署的索爾）的中毒計時設為 11、目前 HP 設為 100（16-bit 寫入，HP 上限不動）；`fdps_chapter_15_init`（`0x21240`）在第 5 步前呼叫 `fdps_units_clear_status_bit7`（`0x2db50`）；`fdps_chapter_24_init`（`0x21490`）把名冊加入排在 reset 之後。

**名冊加入排在最後一次依名冊重建單位陣列之前，新成員才坐得上本章地圖的我方 slot。**reset 會依名冊重建地圖上的我方 slot（見「部署與援軍波次」），slot i 只在 i 小於名冊人數時從名冊第 i 位取人，否則歸零成退場的空 slot；過場腳本的 `SWITCH_MAP` 走的是同一支 reset，所以開場過場最後切回本章地圖的章，決定上場名單的是過場裡的那次重建。所以只有地圖的我方 slot 數多於入隊前名冊人數的章節，順序才決定結果：第 2、3、11 章的開場過場不切地圖，加入排在第 2 步之後，新成員那一格就變成退場的空位；第 1 章（`0x051c` 切回地圖 0）與第 9 章（`0x01a5` 切回地圖 8）的開場過場以 `SWITCH_MAP` 重建本章地圖，加入排在第 2 步之前或之後都一樣，只要在第 3 步之前。第 4、7、8、15、19、24 章的我方 slot 在加入前已被既有成員填滿，新成員兩種順序都不會以名冊記錄上場；原版唯一把加入排在 reset 之後的第 24 章（地圖 23 的 11 個 slot 對 11 人名冊）就屬於這一類。這些「照樣板寫就會錯」的點記在 [`../rebuild_info/pitfalls.md`](../rebuild_info/pitfalls.md)。

`fdps_chapter_state_reset`（`0x22750`）不帶參數，依序：

1. 游標繪製模式 `0x69cd0` ← 0、結束碼 `0x69da0` ← 0。
2. `fdps_build_map_unit_array`（`0x22be0`），參數是章節索引。
3. `memset(0x640d8, 0, 0x20)`：清掉整個 32 byte 的章節事件旗標陣列 `data_fdps_map_cell_event_triggered_flags`（用途見 [`../resource_info/map.md`](../resource_info/map.md)）。
4. 視野原點 `0x69ce4`、`0x69ce0` 與游標世界座標 `0x69cd4`、`0x69ccc` ← 0。
5. 游標繪製模式 ← 1、回合計數器 `data_fdps_battle_turn_counter`（`0x69ce8`）← 1。
6. `fdps_flush_keyboard_queue`（`0x567b3`）。

同一支 reset 也是過場腳本 `SWITCH_MAP` 的實作，所以腳本切換地圖時回合計數器、事件旗標與單位陣列一起重來；展示關 `fdps_title_demo`（`0x2ac10`）把章節索引設成展示關、組好名冊後也呼叫它建立戰場。

`fdps_show_chapter_title_card`（`0x20c60`）從 `MISC.VFS` 取 `Chapter.saf` 與 `Chapter.pal`，以章節索引為 sprite 編號，把標題卡畫在 368×248 的工作頁的 (24, 24)，取其 320×200 視窗貼上畫面。淡入是 bias `−4k` 從 k = 16 走到 k = 0（含兩端，17 步），全亮停 1000 ms，淡出是 k = 1 走到 k = 16（16 步），每一步先等垂直回掃開始、再 `delay(80)`。結束時清空畫面並以 bias 0 載入主調色盤，三塊配置全部釋放。整段不讀鍵盤，無法跳過。

## 部署與援軍波次

戰場單位陣列 `data_fdps_map_unit_array_ptr`（`0x69cd8`）每筆 `0x50` byte，只由兩支 function 建立或加長。

**開場：`fdps_build_map_unit_array`（`0x22be0`）。**先以 `fdps_field_load_chapter_resources`（`0x227e0`）重載章節資源——載入的是章節索引那一章，參數 `map_no` 只拿來組 `map%02d.cod` 的檔名，唯一的呼叫端傳的正是章節索引——然後把 `.CEL` sprite 快取歸零、釋放舊陣列，重新配置 `我方 slot 數 × 0x50` byte（我方 slot 數取自 `MAPnn.DAT` 標頭，見 [`../resource_info/map.md`](../resource_info/map.md)）。對每個 slot i：

- i < 名冊人數：整筆複製名冊第 i 筆，座標取 `MAPnn.COD` 第 **（部署記錄筆數 + i）** 筆的 x、y 的低 byte；sprite 群組以肖像編號（`+0x07`）快取；朝向、步伐、狀態 byte 清 0；陣營設 2；死亡腳本 opcode 設 `0xff`；**HP 與 MP 無條件補滿**；六個狀態計時清 0；重算戰鬥數值。上一章倒下的成員也以滿血上場。
- i ≥ 名冊人數：整筆歸零、狀態 byte 設 1（退場）。

名冊第「我方 slot 數」位以後的成員不上場。最後呼叫 `fdps_deploy_wave(章節索引, 0, 1)` 放下波次 0。

**波次：`fdps_deploy_wave`（`0x23830`）。**開 `ICON.CEL`、開 `Field.vfs` 讀 `map%02d.cod`，依序走過常駐 `MAPnn.DAT` 的全部部署記錄，`+0x15` 的波次 byte（零延伸）等於參數 `wave_no`（32-bit 整數）的每一筆交給 `fdps_deploy_unit`，然後釋放兩個資源。部署記錄永遠讀自目前常駐的那一張 `MAPnn.DAT`，`map_no` 只決定錨點檔；所有呼叫端傳的都是章節索引（第 3 章的處理函式寫字面值 2，值相同）。`Field.vfs` 開不了時印訊息、等按鍵，然後照樣走到載入而結束程式。

**單筆：`fdps_deploy_unit`（`0x232b0`）。**把陣列加長一筆（`realloc`，陣列為空時 `malloc`），新單位接在**陣列尾端**，寫完才把單位數加 1。放置方式由第三個參數決定（值為準，`src/` 的巨集名與行為相反的情形見 [`../rebuild_info/pitfalls.md`](../rebuild_info/pitfalls.md)）：

- 非 0：直接放在錨點（`MAPnn.COD` 第 `deploy_index` 筆）。
- 0：先重建佔用標記（清格、對陣營 0 與 1 各標一次），然後以列優先順序掃整張移動網格，略過有人（旗標 `0x40`）的格，計算曼哈頓距離 `|x − ax| + |y − ay|`，**距離 ≤ 目前最佳**（起始值 `0xff`）且地形類別 < 5 的格就取代目前最佳。平手也取代，所以勝出的是同距離中**掃描順序最後**的一格。沒有任何格合格時，座標變數沒有被寫過，單位落在未初始化的位置（出貨資料走不到，見 map.md「可部署格永遠夠用」）。

數值：角色編號 < `0x3C` 的依 `FRIAPRDA.DAT`／`FRILEVUP.DAT` 以等級算出（公式見 [`../assets/characters.md`](../assets/characters.md)）；`0x3C` 以上的查 `ENEMYDAT.DAT` 第（編號 − `0x3C`）列，HP、MP、AP、DP、DX 都是**等級 × 該列係數**（截成 16-bit），MV 直接取值。HP、MP 目前值與上限相同；陣營、AI 行為、目的地、事件碼、死亡腳本照部署記錄；陣營 2 的經驗值設 0，其他陣營設 `0xff`。欄位對照見 [`../resource_info/map.md`](../resource_info/map.md)。

**單位索引因此是時間順序**：先是我方 slot 0..（slot 數 − 1），接著波次 0 依記錄順序，之後每一次部署依呼叫先後附加。章節處理函式裡寫死的單位索引（「單位 3」「單位 `0x13`」）都依賴這個順序，而不是角色編號。

**誰會部署波次**（`fdps_deploy_wave` 的全部呼叫端）：

| 來源 | 波次 | 放置 |
| --- | --- | --- |
| `fdps_build_map_unit_array`（`0x22be0`） | 0 | 錨點 |
| 過場腳本 `DEPLOY_WAVE`（`fdps_icon_script_run`（`0x21650`）） | 運算元，`0xFF` 表示三選一的答案 | 運算元 |
| 腳本事件處理函式（30 支） | 常數或由回合計數器算出 | 除第 30 章的波次 2、3 外都是找最近空格 |
| `fdps_chapter_19_post_action`（`0x3ae80`）、`fdps_chapter_24_post_action`（`0x3b3d0`） | 決鬥挑戰者的波次 | 找最近空格 |

以回合計數器算波次的處理函式由地圖的回合事件在指定回合呼叫，算式（整數運算，回合計數器是 32-bit 有號）：

| 處理函式 | 波次 |
| --- | --- |
| `fdps_chapter_03_event_deploy_wave_for_turn`（`0x36bb0`） | 回合 − 1 |
| `fdps_chapter_10_event_deploy_wave_for_turn`（`0x37780`） | 回合 = 3 時 1；否則回合 ≤ 13 時 回合 − 4；否則 11 |
| `fdps_chapter_17_event_deploy_wave_for_turn`（`0x38110`） | 回合 − 7 |
| `fdps_chapter_18_event_deploy_wave_for_turn`（`0x38150`） | 回合 |
| `fdps_chapter_23_event_deploy_wave_for_turn`（`0x38a30`） | 回合 ≤ 5 時 回合 + 4；另外回合 3 加部署波次 3、回合 7 加部署波次 4 |
| `fdps_chapter_28_event_deploy_wave_for_turn`（`0x39550`） | 回合 / 2（`SAR` 帶符號修正，向零截斷） |

第 28 章的回合事件排在第 2、4、6、7、10、12、14、16、18 回合，第 6、7 回合都算出波次 3，波次 4 永不出場：這是原版 bug，見 [`known_bugs.md`](known_bugs.md) 第 20 條，被封住的敵兵見 [`../cut_content/story.md`](../cut_content/story.md) 的 S10。

部署記錄的波次 `0xFF` 只有以 255 呼叫才會出場。出貨資料裡帶 `0xFF` 波次的地圖是 `MAP03`、`MAP04`、`MAP07`、`MAP08`、`MAP23`、`MAP27`，而沒有任何呼叫端以 255 部署：過場腳本 `DEPLOY_WAVE` 的波次運算元 `0xFF` 只出現在第 12 章開場過場 `ICON11.DAT` 切到 `MAP11` 之後，代表前面三選一的答案，交給 `fdps_deploy_wave` 的是 1–3；腳本事件處理函式與決鬥的常數波次也沒有 255，上表的算式在回合事件排定的回合也算不出 255，所以這些記錄不會出場。

## 戰鬥中的事件分派

腳本事件表 `0x601c4` 的處理函式由四種資料觸發；觸發條件與時機的格式細節屬於 [`../resource_info/map.md`](../resource_info/map.md)，這裡只寫分派本身。

- **回合事件**：`fdps_battle_run_turn_events`（`0x2e0c0`）掃 `MAPnn.DAT` `+0x03` 起的 16 筆，回合 byte（零延伸）等於回合計數器、陣營 byte 等於參數的每一筆都呼叫，依表內順序、沒有提前結束，單位索引固定傳 0；每一輪都重讀常駐資料的指標。`fdps_battle_advance_turn`（`0x1e3f0`）在 NPC 階段前以 1、敵方階段前以 0、回合計數器加 1 之後以 2 呼叫它，每次之後都接著 `fdps_battle_tick_status_effects` 同一陣營。
- **格子事件**：單位回報停留或經過某格時，`fdps_map_set_pending_tile_event`（`0x2e030`）只對屬性類別 `0x00`、事件碼非 0 的格查 `+0x33 + 2 ×（事件碼 − 1）`，slot 不是 `0xff` 且觸發時機相符就把 slot 存進待處理事件 `data_fdps_chapter_pending_event_idx`（`0x69d90`）。後回報的蓋掉先回報的。會派遣它的迴圈在處理每個單位前把它設為 `0xff`，處理完若不是 `0xff` 就以**該單位的索引**呼叫一次：敵方兩輪與 NPC 一輪對每個單位都做（不論有沒有行動）、`fdps_battle_unit_turn`（`0x15470`）在整個回合結束時做、`fdps_battle_system_menu`（`0x14ab0`）的全軍移動在每個單位移動後做。
- **搜尋**：可搜尋格記錄種類 ≥ 2 時，`fdps_battle_search_cell_at_cursor`（`0x184f0`）以搜尋者索引呼叫該 slot。
- **死亡腳本**：opcode 2 由 `fdps_run_death_scripts`（`0x1d990`）以它收到的擊殺者參數呼叫運算元的 slot；這個參數不一定是擊殺者，各情境傳的是誰見 [`battle.md`](battle.md)。

處理函式收到的參數是觸發者的單位索引。用得到它的處理函式自己檢查身分：出貨地圖選用的 19 支格子事件處理函式裡，有 9 支要求觸發者陣營不是 0（`fdps_chapter_03_event_deploy_wave_14`（`0x36ea0`）、`fdps_chapter_10_event_deploy_wave_10`（`0x378a0`）、`fdps_chapter_19_event_deploy_wave_6`（`0x381d0`））或正好是 2（第 21、25、26、29 章），因為敵方與 NPC 走上事件格同樣會回報；有的再比角色編號或單位索引（第 8、16、20、22、23、25 章）。

**一次性是處理函式自己做的。**格子事件每走上一次就觸發一次，表本身沒有「已觸發」的概念；要只發生一次的處理函式在章節事件旗標陣列 `0x640d8` 裡借一個元素當閂：出貨資料的事件碼只到 15，所以元素 `0x10` 以上沒有格子會用到，處理函式用的是 `0x10`、`0x11`，第 26、27 章另用 `0x12`；第 8 章把 `0x11` 當成逃出村民的計數而不是旗標。這些元素進章節時由 reset 清零、隨存檔保存。

處理函式常做的事有五種，組合起來就是各章的劇情：部署波次（`fdps_deploy_wave`（`0x23830`））、以本章文字區塊畫一行字（`fdps_draw_text`（`0x1ff60`））、改一段單位的 AI 行為（`(行為 & 0xF0) | 新值`，保留高 4 bit，AI 行為的意義見 [`map_ai.md`](map_ai.md)）、移動游標並原地重畫幾幀當鏡頭（`fdps_map_cursor_move_to`（`0x2d7c0`）加 `fdps_render_view_frame`（`0x2beb0`））、直接寫結束碼。事件處理函式裡改一段單位 AI 行為的迴圈都是 `fdps_object_set_field34_low_nibble_range`（`0x36b60`）的 inline 展開，不是呼叫；真正呼叫它的只有 `fdps_chapter_20_post_action`（`0x3b150`）與 `fdps_title_demo`（`0x2ac10`）。

slot 對照（各 slot 屬於哪一章的劇情見 [`../chapters/_index.md`](../chapters/_index.md)）：

| slot | 處理函式 | slot | 處理函式 |
| ---: | --- | ---: | --- |
| 0 | `fdps_chapter_03_event_deploy_wave_for_turn`（`0x36bb0`） | 25 | `fdps_chapter_18_event_deploy_wave_for_turn`（`0x38150`） |
| 1 | `fdps_chapter_03_event_deploy_wave_1`（`0x36c70`） | 26 | `fdps_chapter_19_event_lancelot_joins`（`0x38180`） |
| 2 | `fdps_chapter_event_set_game_over`（`0x36cd0`） | 27 | `fdps_chapter_19_event_deploy_wave_6`（`0x381d0`） |
| 3 | `fdps_chapter_02_event_enemies_advance`（`0x36d00`） | 28 | `fdps_chapter_20_event_upgrade_randis_sword`（`0x382f0`） |
| 4 | `fdps_chapter_03_event_deploy_wave_14`（`0x36ea0`） | 29 | `fdps_chapter_21_event_deploy_wave_1`（`0x38380`） |
| 5 | `fdps_chapter_03_event_turn_limit_game_over`（`0x36f10`） | 30 | `fdps_chapter_21_event_deploy_wave_2`（`0x38400`） |
| 6 | `fdps_chapter_04_event_for_turn`（`0x36f70`） | 31 | `fdps_chapter_22_event_for_turn`（`0x384e0`） |
| 7 | `fdps_chapter_05_event_enemies_advance`（`0x370e0`） | 32 | `fdps_chapter_22_event_boss_defeat`（`0x388b0`） |
| 8 | `fdps_chapter_06_event_deploy_wave_2`（`0x37170`） | 33 | `fdps_chapter_23_event_boss_defeat`（`0x38950`） |
| 9 | `fdps_chapter_07_event_enemies_advance`（`0x37250`） | 34 | `fdps_chapter_23_event_deploy_wave_for_turn`（`0x38a30`） |
| 10 | `fdps_chapter_08_event_for_turn`（`0x372d0`） | 35 | `fdps_chapter_23_event_give_martial_artist_ring`（`0x38b60`） |
| 11 | `fdps_chapter_08_event_send_guest_mage_to_cells`（`0x37440`） | 36 | `fdps_chapter_24_event_deploy_wave_for_turn`（`0x38cc0`） |
| 12 | `fdps_chapter_08_event_villagers_leave_cells`（`0x374e0`） | 37 | `fdps_chapter_25_event_deploy_wave_1`（`0x38e60`） |
| 13 | `fdps_chapter_08_event_villager_escapes`（`0x37600`） | 38 | `fdps_chapter_25_event_upgrade_randis_sword`（`0x38fd0`） |
| 14 | `fdps_chapter_09_event_deploy_wave_1`（`0x37730`） | 39 | `fdps_chapter_25_event_marian_buys_wind_god_bow`（`0x39060`） |
| 15 | `fdps_chapter_10_event_deploy_wave_for_turn`（`0x37780`） | 40 | `fdps_chapter_26_event_enemies_advance`（`0x39190`） |
| 16 | `fdps_chapter_10_event_deploy_wave_10`（`0x378a0`） | 41 | `fdps_chapter_26_event_deploy_waves_2_and_3`（`0x39230`） |
| 17 | `fdps_chapter_11_event_deploy_wave_for_turn`（`0x378f0`） | 42 | `fdps_chapter_26_event_wave_2_defeated_line`（`0x393b0`） |
| 18 | `fdps_chapter_13_event_enemies_advance`（`0x379d0`） | 43 | `fdps_chapter_27_event_deploy_wave_1`（`0x39440`） |
| 19 | `fdps_chapter_14_event_deploy_wave_4`（`0x37a50`） | 44 | `fdps_chapter_28_event_deploy_wave_for_turn`（`0x39550`） |
| 20 | `fdps_chapter_15_event_activate_enemy_group`（`0x37af0`） | 45 | `fdps_chapter_29_event_activate_enemy_groups`（`0x395d0`） |
| 21 | `fdps_chapter_15_event_boss_defeat`（`0x37b70`） | 46 | `fdps_chapter_29_event_activate_all_enemies`（`0x39670`） |
| 22 | `fdps_chapter_16_event_wandering_smith_forge`（`0x37cd0`） | 47 | `fdps_chapter_30_event_deploy_wave_4`（`0x39770`） |
| 23 | `fdps_chapter_16_event_enemies_advance_for_turn`（`0x38020`） | 48 | `fdps_chapter_30_event_deploy_wave_2`（`0x39840`） |
| 24 | `fdps_chapter_17_event_deploy_wave_for_turn`（`0x38110`） | 49 | `fdps_chapter_30_event_deploy_wave_3`（`0x398c0`） |

slot 2 的 `fdps_chapter_event_set_game_over`（`0x36cd0`）只把結束碼設為 1，出貨的 `MAPnn.DAT`（回合事件、格子事件、可搜尋格記錄、死亡腳本）沒有任何一處指到它，見 [`../cut_content/code.md`](../cut_content/code.md) 的 C7；這一格不能從表裡刪掉，見 [`../rebuild_info/pitfalls.md`](../rebuild_info/pitfalls.md#不能換的型別與寫法) 的腳本事件表一列。第 30 章不死者的復活 `fdps_chapter_30_revive_wave_4_undead`（`0x10760`）不在表裡，由 AI 行為 11 直接呼叫。

## 勝敗判定

結束碼 `0x69da0` 是 32-bit：0 進行中、1 敗北、2 過關。寫它的有四種來源，**除了共用判定自己的入口，以及第 19、24 章行動後處理函式在結束碼為 2 時才改回 0 的決鬥開啟，沒有任何一處在寫之前檢查目前的值**，所以同一次行動內誰最後寫誰就是結果：

- 行動後處理函式（每章一支，見下）；
- 腳本事件處理函式：`fdps_chapter_event_set_game_over`（`0x36cd0`）與 `fdps_chapter_03_event_turn_limit_game_over`（`0x36f10`）寫 1，`fdps_chapter_15_event_boss_defeat`（`0x37b70`）、`fdps_chapter_22_event_boss_defeat`（`0x388b0`）、`fdps_chapter_23_event_boss_defeat`（`0x38950`）寫 2；
- 死亡腳本 opcode 4（寫 2）與 5（寫 1），由 `fdps_run_death_scripts`（`0x1d990`）執行（見 [`../resource_info/map.md`](../resource_info/map.md)）；
- `fdps_chapter_state_reset`（`0x22750`）與 `main` 清成 0。

**行動後處理函式的呼叫時機**：敵方兩輪與 NPC 一輪對每個單位處理完（含觸發的格子事件）後各呼叫一次，接著測結束碼非 0 就立刻結束該階段；`fdps_battle_unit_turn`（`0x15470`）在我方單位的回合結束時呼叫一次；`fdps_battle_tick_status_effects`（`0x1fa30`）每次結算中毒、播完死亡動畫後呼叫一次，而它在每個回合對陣營 1、0、2 各跑一次——所以即使沒有人行動，每回合也至少跑三次。全軍移動（`fdps_battle_system_menu`（`0x14ab0`））在各單位之間不呼叫，直接進回合推進。戰鬥結束的讀取點：`fdps_battle_advance_turn`（`0x1e3f0`）在 NPC 狀態結算後、NPC 階段後、敵方狀態結算後、敵方階段後各測一次（我方狀態結算後不測），`fdps_battle_player_phase_loop`（`0x2bae0`）在我方單位行動返回後先測一次（非 0 就不檢查全員是否行動完、不推進回合），並在每一幀的尾端再測，非 0 就結束迴圈回到 `main`。

**共用判定 `fdps_battle_check_default_end_conditions`（`0x3a2e0`）**，依序：

1. 結束碼 ≠ 0 就直接返回，什麼都不看。
2. 結束碼 ← 2。
3. 走過全部單位（索引與單位數都是有號比較），陣營 byte 為 0 且狀態 byte bit 0（退場）為 0 的單位每遇到一個就把結束碼寫回 0；沒有提前結束。
4. 章節索引是 `0x10` 或 `0x15` 時看單位 3，否則看單位 0：`fdps_unit_is_retired`（`0x109b0`）非 0 就結束碼 ← 1，不看目前值。

第 4 步在第 3 步之後無條件執行，所以同一次行動清光敵人又失去主角時結果是敗北。`0x15` 那一支實際上走不到：共用判定的 21 個呼叫端全是行動後處理函式，第 22 章的 `fdps_chapter_22_post_action`（`0x3b270`）不呼叫它。

**各章的行動後處理函式**是下面幾種形狀的組合，內容（看哪個單位、第幾回合）見 [`../chapters/`](../chapters/_index.md)：

| 形狀 | 章 |
| --- | --- |
| 只轉呼共用判定 | 2、7、12、13、14、16、18、21、28、29 |
| 共用判定後，再加「某單位退場 → 1」（不檢查目前值，所以會蓋掉剛寫的 2；第 17 章的共用判定第 4 步已看同一個單位 3，這一寫只在進入時結束碼已非 0 的路徑上改變結果） | 1、4、5、6、9、11、17 |
| 不呼叫共用判定，自己寫勝敗 | 3、8、10、22、23、25、26、27、30 |
| 以事件旗標切換兩種判定（決鬥進行中與否） | 15、19、24 |
| 先依回合改 AI 行為，再轉呼共用判定 | 20 |

不呼叫共用判定的章，「敵人全滅」就不是過關條件，也沒有共用判定入口那道「結束碼已非 0 就不動」的保護。寫入順序決定同時成立時的結果：第 3、10 章是 if／else if，先判敗北；第 25、26、27 章先寫 2 再寫 1，敗北蓋過過關；`fdps_chapter_08_post_action`（`0x3a710`）三條規則依序獨立執行，最後那條（四名村民都已離場）在至少一人逃出時寫 2，會蓋過前面主角退場寫的 1。

**過關可以被撤回。**`fdps_chapter_19_post_action`（`0x3ae80`）與 `fdps_chapter_24_post_action`（`0x3b3d0`）在共用判定之後讀結束碼：若它是 2、回合數未超過期限（第 19 章 ≤ 20、第 24 章 ≤ 25）、旗標元素 `0x11` 未設、裘娜（單位 4）未退場且帶著指定的刀，就部署挑戰者、詢問玩家。接受時把裘娜以外的一段單位設為退場、**把結束碼改回 0**，戰鬥繼續成為一對一決鬥；不論接受與否都設起旗標。旗標設起後處理函式不再呼叫共用判定，改判「裘娜或挑戰者退場 → 2」。第 15 章同樣的決鬥由 `fdps_chapter_15_event_boss_defeat`（`0x37b70`）開啟，由 `fdps_chapter_15_post_action`（`0x3ab30`）以旗標元素 `0x10` 切換。決鬥結束後，結束處理函式把被設為退場的我方恢復（見「章節結束」）。

**挑戰者是寫死的單位索引。**決鬥的勝負判定問的是固定的單位索引——第 19 章 `0x4d`、第 24 章 `0x52`——不是 `fdps_deploy_wave` 剛附加上去的那一筆；只有在陣列當時正好有這麼多單位（該章的援軍波次全部出場過）時，兩者才是同一個單位。接受時的退場掃描也不同：第 19 章掃單位 0..`0x4c`（字面值），第 24 章掃到目前單位數減 1。所以提早過關時，第 19 章的挑戰者落在較小的索引、被掃描一起設為退場，而判定問的 `0x4d` 已超出陣列，戰鬥再也結束不了；第 24 章的挑戰者倖存，但判定問的是陣列之外的 `0x52`，攻略站記載第 7 回合之前過關時表現為挑戰者在第一回合自動認輸。這兩個原版 bug 記在[`known_bugs.md`](known_bugs.md)；重建要照寫死的索引，不可改成追蹤實際部署的單位。

**勝敗條件視窗**：系統選單的 `fdps_battle_system_submenu`（`0x14ea0`）呼叫 `fdps_battle_show_win_fail_window`（`0x17ca0`），顯示章號（索引 + 1）、回合數、隊伍金錢、三個陣營的剩餘人數（`fdps_battle_count_remaining_units_on_side`（`0x18350`）：陣營相符且未退場者，依敵方 0、我方 2、NPC 1 的順序），以及本章文字區塊 `FDETXT(章號)` 的第 2 筆（勝利條件）與第 3 筆（失敗條件）。這兩行字只是顯示，判定完全由上面的程式決定。第 14、20 章的失敗條件文字是「蘭迪斯死亡／法蓮娜死亡」，但這兩章的行動後處理函式只經共用判定看單位 0，沒有任何程式判定法蓮娜——這個原版 bug 記在[`known_bugs.md`](known_bugs.md)，照文字補判定的陷阱見 [`../rebuild_info/pitfalls.md`](../rebuild_info/pitfalls.md)。這個視窗每開一次就重載九張資料表而不釋放舊的，見 [`../rebuild_info/pitfalls.md`](../rebuild_info/pitfalls.md)。

## 章節結束

`main`（`0x29220`）在結束碼 2 時呼叫 `0x60304[章節索引]`，再進村莊階段。結束處理函式的標準形狀是四個呼叫一個寫入，順序不能換：

1. `fdps_battle_destroy_remaining_enemies`（`0x39e10`）：把陣營 0 的每個單位（不論是否已退場）的目前 HP 以 **16-bit** 寫 0，然後無條件呼叫 `fdps_play_death_animation_and_mark_dead`（`0x1d6c0`）把 HP 為 0 且未退場的單位播爆炸、設為退場。第 1、2 章與第 15、19 章沒有這一步。
2. `fdps_roster_write_back_battle_units`（`0x23980`）：名冊寫回。
3. `fdps_icon_script_run`（`0x21650`）播勝利過場，檔名是寫死的字面字串 `WinNN.dat`，NN 是剛打完這章的章節索引。過場裡的 `SWITCH_MAP` 會改寫章節索引、重建單位陣列，所以寫回必須在它之前。
4. `fdps_roster_revive_fallen_members`（`0x39e70`）：陣亡者復活與收費。
5. 章節索引 ← 下一章（字面值）。

**名冊寫回 `fdps_roster_write_back_battle_units`（`0x23980`）**走過戰場上的**每一個**單位（不只我方 slot），對名冊裡角色編號（`+0x08`）相同的每一筆（找到不停，重複的編號兩筆都寫）整筆複製 `0x50` byte，然後：六個狀態計時清 0、狀態 byte `&= 1`、狀態 byte 不是 1 的恢復滿 HP、所有人（含退場者）恢復滿 MP、經驗值（`+0x3c`）大於 99 的歸 0、以名冊公式重算戰鬥數值。唯一的例外是角色編號 0（蘭迪斯）且已退場的單位：跳過，名冊保留原樣——這也讓 `fdps_build_map_unit_array` 產生的歸零空 slot（角色編號 0、退場）不會蓋掉蘭迪斯。

由部署記錄放上場、角色編號與某名冊成員相同的單位也會被寫回，**這就是客串角色以地圖上的數值入隊的方式**：第 4 章的法蓮娜在名冊裡是 `fdps_roster_add_character` 建的初始數值，在地圖上是 `MAP03.DAT` 陣營 2、LV8 的部署記錄，寫回後名冊是後者；第 24 章的珊同理，第 7 回合的 LV15 部署記錄在場就以它入隊，沒等到就保留名冊的 LV10 數值。

**陣亡者復活 `fdps_roster_revive_fallen_members`（`0x39e70`）**走過名冊，目前 HP 為 0 的成員 HP ← 上限、狀態 byte ← 0，並扣隊伍金錢：

`費用 = 復活單價[職業代碼] × 等級`

（職業代碼與等級都是零延伸的 byte，金錢是 32-bit 有號）。**扣款不檢查付不付得起**，直接從金錢減掉；面板顯示完、玩家按鍵關閉後，金錢小於 0 才拉回 0。復活單價依職業代碼 0–25（職業名稱見 [`../assets/classes.md`](../assets/classes.md)）：

| 代碼 | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 單價 | 50 | 100 | 100 | 120 | 70 | 120 | 110 | 70 | 100 | 120 | 40 | 80 | 90 |

| 代碼 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 | 21 | 22 | 23 | 24 | 25 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 單價 | 40 | 80 | 70 | 40 | 100 | 90 | 40 | 110 | 100 | 70 | 100 | 120 | 80 |

面板列出每位復活者的名字、等級與費用；復活者達 7 人以上時一列都不畫，只剩空框等按鍵。沒有人陣亡時什麼都不顯示。復活後 `.CEL` sprite 快取被重建成名冊順序、不還原。

**標準形狀以外的結束處理函式**（各章內容見 [`../chapters/`](../chapters/_index.md)）：

| 處理函式 | 差異 |
| --- | --- |
| `fdps_chapter_01_end`（`0x3a410`） | 不清殘敵；寫回前先以 `fdps_set_flag_bit`（`0x282b0`）替一名單位加一個法術 |
| `fdps_chapter_02_end`（`0x3a470`） | 不清殘敵 |
| `fdps_chapter_15_end`（`0x3ac20`） | 不清殘敵；決鬥旗標已設時先把單位 0–8 的狀態 byte 清 0、恢復一名單位的 AI 行為 |
| `fdps_chapter_18_end`（`0x3ada0`） | 清殘敵前檢查蘭迪斯的道具與轉職狀態，成立時換道具並改播另一支勝利過場 |
| `fdps_chapter_19_end`（`0x3b0b0`） | 不清殘敵；決鬥旗標已設時把角色編號在名冊範圍內的單位狀態 byte 清 0、HP／MP 補滿 |
| `fdps_chapter_23_end`（`0x3b350`） | 清殘敵後把一段範圍內陣營 1 的單位設為退場 |
| `fdps_chapter_24_end`（`0x3b600`） | 同第 19 章的決鬥恢復，之後才清殘敵 |
| `fdps_chapter_26_end`（`0x3b800`） | 清殘敵前，蘭迪斯沒有真炎龍劍且背包未滿就給炎龍劍 |
| `fdps_chapter_27_end`（`0x3b8f0`） | 清殘敵後查兩件道具；都在就移除、播隱藏路線過場、復活、章節索引 ← `0x1b`，**不做名冊寫回**（由該過場的 opcode `0x61` 代做）；缺一件就播結局過場、片尾名單、設離場旗標，章節索引不變 |
| `fdps_chapter_30_end`（`0x3ba80`） | 清殘敵、寫回、勝利過場、強制打開地形面板與遊玩旗標、片尾名單、`delay(30000)`、`GoodEnd.dat`、設離場旗標；不復活、不寫章節索引 |

`fdps_roster_revive_fallen_members` 的呼叫端只有 29 支結束處理函式（第 30 章以外）。

**敗北**時 `main` 只呼叫 `fdps_show_game_over`（`0x2a960`）與 `fdps_title_screen`（`0x2a2b0`），不寫回名冊、不動章節索引；之後由玩家從標題畫面重新開始或讀檔。
