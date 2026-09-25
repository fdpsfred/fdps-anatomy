# 刪減與未用：程式痕跡

程式碼留下的痕跡：沒有呼叫端或沒有來源的函式、全域、旗標、分支，以及存檔檔案佈局。每條寫它在 `src/` 與 Ghidra 的位置、它做什麼、為什麼走不到或沒有內容、它說明了什麼。分類的判定規則見 [`_index.md`](_index.md)；編譯器產物、第三方程式庫（Watcom CRT、Miles AIL）沒用到的 API、不可能觸發的防禦性分支與其實有在用的功能列在那裡的排除清單，不在這裡；遊戲自己的模組裡做好卻沒被呼叫的成員是殘留內容，寫在這裡。

## 被封住的內容

### C1 第 26 章前的神秘商店進不去

分類：被封住的內容｜`src/village.c` `fdps_check_secret_code_key`（`0x357a0`）；`SHOP25.DAT`

- **是什麼**：第 25 章打完、進第 26 章之前的村莊（章節索引 25）照常跑村莊階段，載入 `SHOP25.DAT`。它的神秘商店列（`+0x18`）填了 10 件貨：`B7 B8 B9 D2 D3 D4 D6 D7 D8 D9`。神秘商店只有在看板選單輸入該章的暗號、`fdps_check_secret_code_key` 回 1 時才會出現。
- **為什麼到不了**：暗號表只有 24 列（章節索引 1–24），以「章節索引 − 1」取列。索引 25 取到表外的第 25 列，落在這支函式自己的回傳值槽與存下的 EBP 上，穩態下兩半都是 `fdps_village_signboard_menu` 的 EBP，也就是一個堆疊位址的 byte。這組「暗號」不是設計出來的按鍵序列，玩家無從得知，照常遊玩進不去；攻略站第 26 章也記「神秘商店：無法進入，只能透過修改」。成因與現象見 [`known_bugs.md`](../program_info/known_bugs.md) 第 19 條。
- **不算的部分**：表內全 0 的四列（章節索引 16、17、21、22）剛好是沒有村莊的章，那四章沒有神秘商店可封。
- **暗示**：商店資料替這座村莊準備了神秘商店的貨，暗號表卻只做到第 25 章（攻略站的修改頁也寫「由第 2 章開始到第 25 章止」）。那 10 件貨是什麼、別處拿不拿得到，見道具主題檔的 [I09](items.md#i09-第-26-章前神秘商店的貨)。
- 「穩態」的例外是 `fdps_cd_music_repeat_poll`（`0x30c50`）每 0x4b 輪向光碟機詢問狀態的那一輪（只在有選曲且音樂開著時）：那一格前 4 byte 會變成 `fdps_cd_audio_is_idle`（`0x3c6e8`）內部 CALL 留下的返回位址（`0x3c6f7` 在執行期被 DOS/4GW 搬移後的線性位址），若接著重播音軌則是 poll 內 `0x30ca4` 的返回位址，下一輪由 `free` 的 PUSH EBP 寫回選單的 EBP。物件以整頁搬移，兩者的最低 byte 仍是 `F7`、`A4`，超出看板選單會送進來的掃描碼範圍（小於 `0x7f`），不能作為暗號開頭，不改變進不去的結論。
- 執行期的 EBP 值沒有實測；不論它是多少，這組鍵都不是設計好的暗號。重建時不能「修好」它，見 [`pitfalls.md`](../rebuild_info/pitfalls.md) 的村莊暗號表一列。

## 殘留內容

### C2 AI 行為模式 9：追擊指定角色

分類：殘留內容｜`src/mapai.c` `fdps_map_actor_behavior_step`（`0x10010`，模式 9 的比較在 `0x104cb`）

- **是什麼**：行為模式 9 把單位記錄的 `ai_dest_x`（`+0x35`）當成角色編號，找到那個單位就走向他；找不到或走不動時依序退回最佳行動、走向最近的可達敵人、走向最近的敵人、休息。程式完整可用。
- **為什麼到不了**：單位的行為模式只有兩個來源。部署記錄（`+0x11`，由 `fdps_deploy_unit` 整 byte 複製）在 63 個 `MAP*.DAT` 裡只出現 0、1、2、3、5、7、8、11（`MAP31.DAT` 筆數以外的 11 筆也全是 0）；程式寫進 `+0x34` 的模式只有 0、3、4、7、`0xA`、`0xB`。沒有任何來源讓單位進入模式 9。行為模式的資料格式見 [`map.md`](../resource_info/map.md)。
- **模式 6 是空號**：分派鏈比較 0–5 與 7–11，沒有 6，落進去就什麼都不做；`0xC`–`0xF` 同樣沒有分支。FD2 同樣沒有模式 6，這個缺口是繼承來的編號。
- **來歷**：模式 9 承自 FD2，而且 FD2 的出貨資料用過它（`FDFIELD.DAT` 第 18 章兩筆 `0x89`）。FDPS 改寫了它走不動之後的退路鏈——FD2 走不動就休息——而其他從 FD2 繼承的模式沒有這樣改，所以 FDPS 開發時動過這個模式，出貨資料卻一筆都沒用。FDPS 改寫過卻沒有啟用，依分類規則是殘留內容而不是前作遺留；改寫是為了某個後來撤掉的關卡還是順手調整，映像與資料判斷不出來。

### C6 兩種沒用上的全螢幕轉場

分類：殘留內容｜`src/transit.c` `fdps_transition_box`（`0x2f410`）、`fdps_transition_slide`（`0x2f6d0`）

- **是什麼**：兩支做好的全螢幕轉場。`fdps_transition_box` 有兩種樣式（style 0 收縮、其他值展開）；`fdps_transition_slide` 以 `0x2f6e0` 的八項跳躍表分派八個方向。
- 八個方向並非全都正確：style 6（由右側滑入）的迴圈照其他三個水平方向數到 width，每幀的落點卻是 `dst + (height - 位置)`（`0x2fa76` 加的是 height），非正方形的矩形畫面就會錯，在 320×200 上位置超過 200 後落點跑到 `dst` 之前。因為 `fdps_transition_slide` 沒有呼叫點，這個原版錯誤從未在遊戲中出現。
- **為什麼到不了**：整個映像沒有任何呼叫或指標引用它們——Ghidra 沒有 xref，記憶體中沒有兩者位址的 dword，原檔的相對 `CALL`／`JMP` 也沒有落在兩者入口的。遊戲實際用的轉場是同一模組的 `fdps_transition_random_blocks` 與 `fdps_transition_zoom`。
- **暗示**：FD2 沒有帶樣式參數的通用 box／slide 轉場，這是 FDPS 新做而沒接上的效果。`transit.c` 是遊戲自己的模組，不是第三方程式庫，做好卻沒被呼叫的成員歸殘留內容。

### C7 沒有任何地圖選用的判負處理函式

分類：殘留內容｜`src/chevt1.c` `fdps_chapter_event_set_game_over`（`0x36cd0`）；腳本事件表 `0x601c4` 第 2 格（`src/chapter.c`）

- **是什麼**：無條件把戰鬥結束碼寫成 1（判負）的事件處理函式，不畫任何文字。
- **為什麼到不了**：地圖資料只以 slot 號碼從腳本事件表選處理函式，來源有四種：回合事件、格子事件、死亡腳本 opcode 2、搜尋格 kind ≥ 2。全部 `MAP*.DAT` 普查的結果，50 格裡只有第 2 格沒被任何一種來源引用；Ghidra 也只有表內 `0x601cc` 一個引用，沒有直接呼叫。盜賊偷寶箱時改寫死亡腳本也限制 opcode < 2，不會產生 opcode 2。四種來源的格式見 [`map.md`](../resource_info/map.md)。
- **暗示**：出貨資料的判負全部走別的路徑——死亡腳本 opcode 5（`MAP00`、`01`、`03`、`04`、`05`、`07`、`14` 各一筆，先畫台詞再判負）、預設勝敗檢查與各章行動後處理函式直接寫 1，以及第 3 章 `MAP02` 回合事件選用的第 5 格 `fdps_chapter_03_event_turn_limit_game_over`（帶台詞與增援）。沒有台詞的通用判負格留在表裡沒人選。
- 這一格不能從表裡刪掉，見 [`pitfalls.md`](../rebuild_info/pitfalls.md) 的腳本事件表一列。

### C11 沒接上的 raw PCM 播放介面

分類：殘留內容｜`src/audio.c` `fdps_audio_start_sample`（`0x30630`）、`fdps_audio_set_sample_playback_rate`（`0x30810`）；全域 `data_fdps_audio_sample_playback_rate`（`0x69d5c`）

- **是什麼**：一組可運作的 raw PCM 播放介面。`fdps_audio_start_sample` 找一個空閒的 sample handle，直接播呼叫端給的 PCM 緩衝（不設格式與音量），取樣率讀 `0x69d5c`；`fdps_audio_set_sample_playback_rate` 是唯一寫那個全域的函式。
- **為什麼到不了**：兩支都沒有呼叫或位址引用；`0x69d5c` 只被這兩支各碰一次（一讀一寫）。
- **暗示**：FD2 的音效播放是從音效庫目錄取樣本，FD2 裡沒有這組介面，也沒有取樣率全域，所以不能歸前作遺留。`audio.c` 是遊戲自己的音訊包裝層，不是第三方程式庫，做好卻沒被呼叫的成員歸殘留內容。

### C16c MSCDEX 存取層沒用到的 19 支

分類：殘留內容｜`src/cd.c`、`src/cdtoc.c`、`src/cdaudio.c`

- **是什麼**：遊戲自己的 MSCDEX 存取層（[`cd_audio.md`](../program_info/cd_audio.md)）裡做好的 CD 命令包裝。零引用的 16 支：`src/cd.c` 的 `fdps_cd_read_head_sector`、`fdps_cd_read_audio_channel_info`、`fdps_cd_set_audio_channel_control`、`fdps_cd_close_tray`、`fdps_cd_read_media_change_status`、`fdps_cd_set_door_lock`；`src/cdtoc.c` 的 `fdps_cdrom_read_upc`、`fdps_cd_get_track_length_msf`、`fdps_cd_get_disk_info_msf`、`fdps_cd_sector_to_msf`、`fdps_cd_track_is_audio`；`src/cdaudio.c` 的 `fdps_cd_seek`、`fdps_cd_resume_audio`、`fdps_cd_play_track_range`、`fdps_cd_play_whole_disc`、`fdps_cd_read_audio_position`。
- **為什麼到不了**：上面 16 支沒有任何呼叫或位址引用；另外 3 支只被它們呼叫：`fdps_cd_ioctl_output_command`（唯一呼叫點在 `fdps_cd_close_tray` 內）、`fdps_cd_get_track_length_sectors`（在 `fdps_cd_get_track_length_msf` 內）、`fdps_cd_read_q_channel`（`0x3c5a6`，在 `fdps_cd_read_audio_position` 內），共 19 支走不到。其餘 CD 輔助函式都至少有一個呼叫點落在使用中的函式裡。
- **暗示**：存取層包了 MSCDEX 的多數命令（關門、鎖門、UPC、依範圍或整片播放、讀播放位置），遊戲的音樂與影片只用到其中一部分。這是遊戲自己的模組，不是第三方程式庫，做好卻沒被呼叫的成員歸殘留內容。

### C16d VFS 讀取器沒用到的 3 支

分類：殘留內容｜`src/vfs.c` `fdps_vfs_read_entry_count`（`0x398f0`）、`fdps_vfs_image_entry_count`（`0x39960`）、`fdps_vfs_find_entry_size`（`0x39a20`）

- **是什麼**：`.VFS` 容器讀取器（[`vfs.md`](../resource_info/vfs.md)）裡三支查詢函式：讀容器檔的成員數、取已載入映像的成員數、以名稱查成員大小。
- **為什麼到不了**：三支都沒有 xref。遊戲只經由開啟、查表與讀取成員的那幾支使用容器。
- **暗示**：讀取器是遊戲自己的模組（容器簽章是開發商的 `Dynasty Information Co.,`），不是第三方程式庫，做好卻沒被呼叫的成員歸殘留內容。

### C16e RLE 繪製走不到的 6 種模式

分類：殘留內容｜`src/blit.h`、`fdps_blit_dispatch`（`0x568db`）

- **是什麼**：RLE 繪製的 13 個 kernel 裡有 6 個走不到：mode 1（sprite 與背景一起調色盤重映射）、2（調色盤重映射）、5（旋轉）、6（旋轉加縮放）、7（水平鏡像）、12（限定色域的半透明）。它們都是做好的手寫組語 kernel，與用得到的 0、3、4、8、9、10、11 一起編進映像。
- **為什麼到不了**：每條到達 dispatcher 的路徑，mode 參數都解成常數，集合只有 0、3、4、8、9、10、11（普查記在 `0x56a8d` 的 plate comment）。
- **暗示**：繪製器是遊戲自己的模組，不是第三方程式庫，做好卻沒被呼叫的模式歸殘留內容。它也不是原封不動的前作程式：FD2 的 RLE 繪製是一支解碼器加三種調色盤模式（原樣、半透明疊加、剪影），另有一支調色盤重映射函式，沒有這種以 mode 分派 13 個 kernel 的 dispatcher，旋轉與縮放在 FD2 也沒有對應；即使調色盤重映射的做法在 FD2 就有，在 FDPS 也已改寫進新的 dispatcher 介面。重建時這 6 個 kernel 照樣要編進去，dispatcher 與 kernel 的交接見 [`architecture.md`](../program_info/architecture.md)。

## 前作遺留

### C3 AI 行為 byte 的兩個高位旗標

分類：前作遺留｜`src/mapai.c`（`0x12c72`）、`src/aiscore.c`（`0x13380`）；單位記錄 `+0x34`

- **是什麼**：行為 byte 的高 4 bit 有兩個旗標有讀取端。`0x40`：物理攻擊與法術平手（法術編號 ≥ `0x12` 時才看旗標，以下改比威力）、或物理攻擊與道具平手時，改走物理攻擊。`0x80`：HP 回復道具（使用效果 `0x0B`）對該目標的評分乘 3。
- **為什麼沒作用**：沒有任何程式把它們設起來。對 `+0x34` 的 byte 寫入只有立即數 7、立即數 0、`fdps_deploy_unit` 從部署記錄整 byte 複製，以及各章事件以 `(old & 0xF0) | mode` 改模式——合併進來的 mode 全都小於 `0x10`。63 個 `MAP*.DAT` 共 1,676 筆部署記錄，這個 byte 全在 0..`0x0B`。唯一可能非 0 的來源是名冊：`fdps_roster_add_character` 從不寫 `+0x34`，名冊來的單位帶著 malloc 區塊（示範戰之後是已釋放的區塊）原有的內容，所以「兩旗標恆為 0」只對部署記錄產生的單位是資料保證。
- **來歷**：同一個 byte、同一個位移、同樣的平手規則（FD2 的法術門檻是 `0xB`）、同樣的乘 3、同樣的 `&0xF0` 合併寫法，都與 FD2 逐項相同；FD2 的 `FDFIELD.DAT` 1,887 筆部署記錄裡有 97 筆設 `0x80`、3 筆設 `0x40`。FDPS 重做的地圖資料一筆都沒用。改模式時保留高 4 bit 是沿用 FD2 的寫法，不是替還沒填的內容預留。
- 名冊不初始化這個 byte 是重建時的雷，見 [`pitfalls.md`](../rebuild_info/pitfalls.md) 的名冊 `+0x34` 一列。

### C4 單位 byte +5 的 bit 2

分類：前作遺留｜`src/btlmenu.c` `fdps_battle_system_menu` 內的 `0x14c9c`、`src/btlturn.c` `fdps_battle_player_phase_loop` 內的 `0x2bc89`

- **是什麼**：這兩處以 `AND AL,0x85` 遮罩單位記錄的 byte +5，非 0 就跳過該單位——前者是「全員前進」，後者是我方單位循環。遮罩裡的 bit 2 是唯一不代表退場（bit 0）或本回合已行動（bit 7）的一位。
- **為什麼沒作用**：整個映像沒有寫 bit 2 的地方。所有對 +5 的寫入只產生 bit 0 與 bit 7，整筆複製的來源（名冊、遊戲自己寫的存檔）也只帶這兩位，所以 bit 2 執行期恆為 0。byte +5 的逐位語意見 [`architecture.md`](../program_info/architecture.md)。
- **來歷**：FD2 在對應的兩處（全員前進、下一名可行動角色的循環）用同一個 `0x85` 遮罩。FD2 知識庫把 bit 2 叫「不能行動」（`CHARFLAG_CANNOT_ACT`），但 FD2 原始碼同樣找不到設它的地方、這個常數也沒被使用，那個名字只是前作的推定。FD2 端只比到原始碼層，沒有在 FD2 的映像上逐指令確認。

### C5 「取消即結束回合」旗標

分類：前作遺留｜`src/btlturn.c` `data_fdps_battle_action_cancel_ends_turn_flag`（`0x60004`），讀取在 `fdps_battle_unit_turn` 的 `0x156b2`、`0x157ae`

- **是什麼**：在行動選單按取消時，這個全域決定單位退回原位重選（0）還是標成已行動、結束回合（非 0）。
- **為什麼沒作用**：只有這兩處讀取，整個映像沒有寫入端（xref 與位址 byte 搜尋都只找到這兩條 `CMP`），初值 0。取消一律退回，「取消即結束回合」的分支是死碼。
- **來歷**：FD2 的 `data_fd2_battle_player_action_result_code`（`0x53C53`）是同形的全域：每回合開頭清 0，戰鬥中把道具交給別人時設 1，取消後依它決定退回或結束回合。FDPS 把「交付道具後取消即結束回合」改由 `fdps_battle_action_menu` 的區域變數處理（道具選單回報已交付時，取消的回答改成完成），拿掉了全域的寫入端，讀取卻留了下來。FD2 的對應是由程式形狀（同樣位置的讀取、同樣的 0＝退回／非 0＝結束）推定，沒有逐指令比對兩邊的組語。

### C8 存檔的第 4 個 slot

分類：前作遺留｜`src/save.h` `SAVE_SLOT_COUNT`、`src/save.c`、`src/savepnl.c`、`src/savefile.h`；`FDE.SAV` `+0x4FA3`

- **是什麼**：`FDE.SAV` 的 `0x59CB` byte 映像在 `+0x312B` 之後排了 4 個 `0xA28` byte 的章節 slot。
- **為什麼沒有內容**：存讀檔畫面的游標以 3 取餘數（`0x246e5`），面板迴圈只畫 3 格（`0x2490c`），第 4 個 slot 永遠選不到。以 slot 欄位定址它的指令只有一處：戰鬥選單的存檔在 `FDE.SAV` 不存在時把 4 個 slot 的章節 byte 都寫成 `0xFF`（第 4 個在 `0x1514b`）。存讀檔畫面的章節存檔（`fdps_save_game_screen`）在檔案不存在時則以 `memset` 把整份映像連同第 4 格填成 `0xFF`（`0x242dc`）再寫回。兩條存檔路徑寫入的整檔檢查碼（`+0x59C7`）落在它尾端的保留區裡。除此之外沒有程式以欄位讀寫它；整份映像的讀寫、加解密與檢查碼計算只是連帶經過它的 byte。欄位佈局見 [`save.md`](../resource_info/save.md)。
- **來歷**：這個檔案佈局與 FD2 的 `FD2.SAV` 逐項相同（同樣 `0x59CB`、4 個 slot、檢查碼在 `+0x59C7`），而 FD2 的存檔選擇游標走 0..3，四格都選得到。第 4 格是 FDPS 把畫面上的 slot 減成 3 格之後原封不動留下的前作佈局。
- 檔長與檢查碼位置不能照畫面的 3 格重算，見 [`pitfalls.md`](../rebuild_info/pitfalls.md) 的 `FDE.SAV` 一列。

### C9 MIDI 背景音樂的初始化

分類：前作遺留｜`src/audio.c` `fdps_audio_init`（`0x304e0`）、`src/audio.h`；全域 `0x69d72`、`0x69d6c`、`0x69d60`

- **是什麼**：啟動時照 FD2 `main` 的同一段寫法安裝 MDI 驅動（`AIL_install_MDI_INI`），成功時設旗標並配一個 sequence handle。
- **為什麼沒作用**：三個相關全域的全部參照都在這支函式裡；整個映像沒有 `AIL_init_sequence`、`AIL_start_sequence` 的呼叫端，遊戲檔也沒有任何 XMIDI 音樂資料。遊戲目錄只附 `DIG.INI` 與 `.DIG` 驅動，沒有 `MDI.INI` 也沒有 `.MDI` 驅動，所以在原版的安裝上 MDI 安裝本來就會失敗（[`ail_link.md`](../rebuild_info/ail_link.md)）。
- **來歷**：FD2 在 MDI 驅動存在時以 `FDMUS.DAT` 載入 sequence、用 `AIL_init_sequence` 與 `AIL_start_sequence` 播背景音樂。FDPS 的音樂改走 CD 音軌（[`cd_audio.md`](../program_info/cd_audio.md)），這半段是 FD2 播 MIDI 的遺留。
- 隨附的 `SETSOUND.EXE` 是 Miles AIL V3.02（18-Jan-95）的通用音效設定程式，照樣提供「Select and configure MIDI music driver」選單並能寫出 `MDI.INI`，`AILDRVR.LST` 也列有 17 個 `.mdi` 驅動項目（`sbpro2.mdi`、`opl3.mdi` 各出現兩次），但遊戲目錄一個 `.MDI` 檔都沒附。即使玩家自備驅動讓 `MDI.INI` 存在，也只會讓 `fdps_audio_init` 的安裝與 sequence handle 配置成功，映像裡仍沒有任何播放端。

### C10 前作封裝檔的讀取器

分類：前作遺留｜`src/rsrc.c` `fdps_load_indexed_archive_entry`（`0x22e30`）

- **是什麼**：讀「6 byte 檔頭加 u32 位移表」的封裝檔成員：釋放舊緩衝、開檔、seek 到 `index × 4 + 6`、讀相鄰兩個位移相減得長度、配置、讀入。
- **為什麼沒作用**：零引用——Ghidra 沒有 xref，映像中沒有它的位址常數，`src/` 沒有呼叫點。FDPS 的出貨檔案沒有這種封裝檔，資源全部經由 `.VFS`（[`vfs.md`](../resource_info/vfs.md)）。
- **來歷**：讀取步驟與 FD2 的 `.DAT` 資源載入器 `fd2_load_dat_resource` 相同，但介面與錯誤處理不同：結果與位移暫存都經由呼叫端給的指標槽、不寫長度全域、拿掉了 malloc 失敗的檢查、找不到檔的訊息多了一個 BEL。它在 FD2 之後被改寫過，但改寫是在 FDPS 開發期間，還是在兩作之間某個作品就已完成，映像判斷不出來。分類規則要確定是 FDPS 改寫過才歸殘留內容，這一點沒有證據，所以歸前作遺留；FDPS 也沒有任何這種格式的內容。

## 否定性結論

### C13 沒有除錯鍵、作弊碼與啟動開關

分類：否定性結論｜`src/main.c`、`src/keybd.c`、`src/btlturn.c`、`src/village.c`、`src/walk.c`、`src/audio.c`

- 遊戲碼沒有除錯鍵、作弊碼、命令列參數或環境變數開關。`main` 不讀 `argc`／`argv`，啟動只讀 `DISK.NO`；遊戲自己的程式碼不呼叫 `getenv`（原版 `getenv` 的七個呼叫者全在 Miles AIL 與 CRT）。唯一的按鍵序列比對是村莊看板選單的神秘商店暗號：`fdps_village_signboard_menu`（`0x31bc0`）把每個按下的掃描碼送進 `fdps_check_secret_code_key`（`0x357a0`），逐鍵比對該章那一列（24 列、每列 8 byte 的掃描碼表，映像 `0x310c0`）。那是設計好的遊戲功能（見 C1），不是作弊碼；其餘按鍵讀取點都只比對單一掃描碼。
- 遊戲認的鍵：方向鍵、Enter、Space、Esc、Z、Del、數字鍵 5、Tab（村莊看板）、F1、F2／Home，以及單位移動動畫中按住可以快轉的主鍵盤 1、2（掃描碼 `0x02`、`0x03`，只在 `src/walk.c` 的四支移動步進函式裡比對，攻略站與遊戲文字都沒有提到），另有幾個按任意鍵繼續的畫面。神秘商店的暗號另外用到字母、主鍵盤數字 1–5 與 0、`.`、`-`（例如第 7 章是 1 2 2 1 1）。
- 唯一能由使用者打開的除錯輸出屬於連進來的 Miles AIL 程式庫：設了環境變數 `AIL_DEBUG`（加上 `AIL_SYS_DEBUG` 另開巢狀呼叫記錄），`AIL_startup` 就寫出 AIL 用量記錄檔。`fdps_audio_init` 每次啟動都呼叫 `AIL_startup`，所以這個開關在原版照樣生效；它是第三方程式庫的除錯功能，不是遊戲的。

### C15 沒有未被引用的字串

分類：否定性結論｜`src/church.c`

- `FDPS.LE` 三個 object 裡長度 3 以上、以 NUL 結尾的字串，全部有 reference 指到字串本身或它的內部。
- 唯一看似孤兒的 `magic0bb.saf`（`0x310ac`）是 `fdps_church_promote_unit` 的自動陣列 `effect_names` 的初值範本（`0x31098` 起 40 byte，兩格各 20 byte）的第二格：範本由 `0x34c41` 載入、`0x34c46` 以 `REP MOVSD` 複製到堆疊，再以新職業碼查表取第 0 或第 1 格。有 8 個職業碼選到它，`MISC.VFS` 裡也有 `MAGIC0BB.SAF` 這個成員。
- 字串池裡另有長度 1 到 2 的片段沒有 reference，例如 `0x619ca` 與 `0x61a76` 的 `r`、`0x61fad` 的 `el`、`0x61f3d` 的 `+`：它們都落在前一個字串的 NUL 與下一個 4 byte 對齊字串之間的填充空隙，是編譯器對齊填充留下的殘餘 byte，不是字串；真正在用的 `+` 在 `0x615ac`，由 `src/text.c` 的 `fdps_draw_number` 讀取。
