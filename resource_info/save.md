# `FDE.SAV` 存檔格式

遊戲目錄下的 `FDE.SAV` 是唯一的存檔檔，固定 22,987 byte（`0x59cb`）。檔案本身沒有 magic、版本或長度欄位；磁碟上的內容是整份映像經過 XOR 串流加密的結果，加密前在尾端放一個 32-bit 加總檢查碼。

檔案分兩區，由不同的程式寫、不同的程式讀，彼此不解讀對方的內容：

- **live-state**（`+0x0000`–`+0x312a`）：戰鬥中存檔的整場戰鬥狀態，戰鬥選單的「存檔」寫、戰鬥選單的「讀檔」與標題選單的「繼續」讀。
- **slot 區**（`+0x312b` 起 4 個 `0xa28` byte 的 slot）：章節之間的隊伍狀態，存讀檔畫面寫與讀。存讀檔畫面只選得到前 3 個，第 4 個 slot 是空殼。

兩條路徑都是「整檔讀進來、改自己那一區、重算檢查碼、整檔加密寫回」，所以寫一區會原樣保留另一區。

欄位的 C 定義：slot 是 `struct fdps_save_slot`，roster 與地圖單位的記錄是 `struct fdps_unit_record`（`src/fdpstype.h`，欄位語意見 [`program_info/data_structures.md`](../program_info/data_structures.md)）。讀寫工具見 [`tools/save_format/`](../tools/save_format/_index.md)。

## 碰這個檔的 function

`"FDE.SAV"` 字面值（`0x60118`）只有下表前六支引用，加密常式 `fdps_xor_crypt_buffer`（`000568b7`）的 8 個呼叫點與檢查碼常式 `fdps_compute_save_checksum`（`00056898`）的 4 個呼叫點也全部落在這六支裡。第七支 `fdps_draw_save_slot_panel` 不開檔，拿到的是呼叫端已經讀進記憶體、解密過的 slot。

| function | 位址 | 入口 | 讀／寫 | 碰哪一區 | 驗檢查碼 |
| --- | --- | --- | --- | --- | --- |
| `fdps_battle_system_submenu` | `00014ea0` | 戰鬥中系統選單的「存檔」 | 讀後整檔寫回 | 寫 live-state | 不驗 |
| `fdps_load_savegame` | `00023e20` | 戰鬥中系統選單的「讀檔」、標題選單第三項（繼續） | 讀 | 讀 live-state | 驗，不符照樣讀入 |
| `fdps_title_screen` | `0002a2b0` | 開機的標題選單 | 讀 | 讀 live-state 的章節 byte | 驗，決定選單項目 |
| `fdps_save_game_screen` | `000241e0` | 無村莊章節之間的存檔提示、酒館的「存檔」 | 讀後整檔寫回 | 寫一個 slot | 不驗 |
| `fdps_load_game_screen` | `00024490` | 標題選單第二項（讀檔）、酒館的「讀檔」 | 讀 | 讀一個 slot | 不驗 |
| `fdps_saveload_screen_build` | `00024830` | 兩個存讀檔畫面組版面 | 讀 | 讀 slot 0–2 的章節 byte | 不驗 |
| `fdps_draw_save_slot_panel` | `00024a40` | 上一支對 slot 0–2 各呼叫一次 | 讀（已在記憶體） | 讀一個 slot 的摘要欄位 | 不驗 |

`fdps_battle_system_submenu` 另外以 `access("FDE.SAV", 0)` 探測檔案存在與否，不存在時把選單的「讀檔」反灰。

## 整體佈局

| 偏移 | 大小 | 內容 |
| ---: | ---: | --- |
| `+0x0000` | `0x08a3` | live-state：地圖腳本區塊 |
| `+0x08a3` | `0x0a00` | live-state：roster，32 筆 `0x50` byte 單位記錄 |
| `+0x12a3` | `0x1e00` | live-state：地圖單位陣列，96 筆 `0x50` byte 單位記錄 |
| `+0x30a3` | `0x0020` | live-state：格子事件已觸發旗標 |
| `+0x30c3` | `0x0012` | live-state：戰鬥狀態標頭 |
| `+0x30d5` | `0x0056` | 不屬於任何欄位：沒有程式指定它的內容，只隨整檔讀寫原樣帶過 |
| `+0x312b` | `0x0a28` | slot 0 |
| `+0x3b53` | `0x0a28` | slot 1 |
| `+0x457b` | `0x0a28` | slot 2 |
| `+0x4fa3` | `0x0a28` | slot 3（空殼） |
| `+0x59c7` | 4 | 檢查碼（u32 LE），與 slot 3 的最後 4 byte 重疊 |

`0x312b + 4 × 0xa28 = 0x59cb`，各區首尾相接、中間沒有對齊填充。多 byte 欄位一律 little-endian，而且不對齊（金錢在 `+0x30cd`、檢查碼在 `+0x59c7`）。

## live-state

五個區塊由 `fdps_battle_system_submenu` 依序寫入、由 `fdps_load_savegame` 以同樣的偏移讀回。

| 偏移 | 大小 | 來源（寫）→ 去處（讀） | 寫入者 | 讀取者 |
| ---: | ---: | --- | --- | --- |
| `+0x0000` | `0x8a3` | `data_fdps_tile_event_data_table_ptr`（`0x6013c`）指向的緩衝區 | `fdps_battle_system_submenu` | `fdps_load_savegame` |
| `+0x08a3` | `0xa00` | `data_fdps_roster_array_ptr`（`0x64108`）指向的 roster | `fdps_battle_system_submenu` | `fdps_load_savegame` |
| `+0x12a3` | 單位數 × `0x50` | `data_fdps_map_unit_array_ptr`（`0x69cd8`）指向的地圖單位陣列 | `fdps_battle_system_submenu` | `fdps_load_savegame` |
| `+0x30a3` | `0x20` | `data_fdps_map_cell_event_triggered_flags`（`0x640d8`） | `fdps_battle_system_submenu` | `fdps_load_savegame` |
| `+0x30c3` | `0x12` | 戰鬥狀態標頭，見下表 | `fdps_battle_system_submenu` | `fdps_load_savegame`；`+0x02` 另由 `fdps_title_screen` 讀 |

**地圖腳本區塊**是當章 `MAP%02d.DAT` 讀進來的那塊記憶體（格式見 [`map.md`](map.md)），存檔時不管成員實際多大，一律從它的開頭搬 `0x8a3` byte。`MAP%02d.DAT` 最大正好是 `0x8a3`（`0x83` 的固定部分加 80 筆 `0x1a` byte 的部署記錄），較小的成員之後的 byte 是存檔當下堆積上緊鄰的內容。讀檔時配置一塊 `0x8a3` 的新緩衝區整塊搬回，再從它的 `+0x01`、`+0x02` 重新取出我方 slot 數與部署記錄筆數。

**roster** 是章節開始時的隊伍樣板，不是戰場上的即時數值——戰場上的即時狀態在地圖單位陣列裡。整塊 `0xa00` 照搬，有效筆數由標頭 `+0x09` 給。

**地圖單位陣列**只寫「單位數 × `0x50`」byte，單位數以外到 `0x1e00` 為止的部分保留檔案原有的內容。讀檔時配置整塊 `0x1e00`、搬回單位數 × `0x50` byte，然後替每個單位重新載入圖示並把新的快取槽號寫進記錄的 `+0x02`，檔案裡的槽號因此不會被使用。單位數沒有上限檢查，標頭寫超過 96 會讓讀檔越界寫入。

**格子事件已觸發旗標**以格子事件碼為索引，每格一個 byte；章節處理函式借用其中幾格當一次性事件的閂鎖，所以存讀檔後已觸發的事件不會再觸發。

### 戰鬥狀態標頭（`+0x30c3`，`0x12` byte）

寫入者一律是 `fdps_battle_system_submenu`（`000151c8`–`000152a1`），讀取者一律是 `fdps_load_savegame`（另註明者除外）。

| 標頭偏移 | 檔案偏移 | 型別 | 內容 | 對應的全域 |
| ---: | ---: | --- | --- | --- |
| `+0x00` | `+0x30c3` | u8 | 回合數 | `data_fdps_battle_turn_counter`（`0x69ce8`） |
| `+0x01` | `+0x30c4` | u8 | 地圖單位數 | `data_fdps_map_unit_count`（`0x60150`） |
| `+0x02` | `+0x30c5` | u8 | 章節索引；`0xff` 表示這個檔沒有戰鬥存檔，`fdps_title_screen` 也讀這個 byte | `data_fdps_chapter_current_chapter_id`（`0x69cf4`） |
| `+0x03` | `+0x30c6` | u8 | 畫面視窗原點 x，以格為單位 | `data_fdps_battle_view_window_origin_x`（`0x69ce4`） |
| `+0x04` | `+0x30c7` | u8 | 畫面視窗原點 y，以格為單位 | `data_fdps_battle_view_window_origin_y`（`0x69ce0`） |
| `+0x05` | `+0x30c8` | u8 | 地圖游標 x，以格為單位 | `data_fdps_map_cursor_world_x`（`0x69cd4`） |
| `+0x06` | `+0x30c9` | u8 | 地圖游標 y，以格為單位 | `data_fdps_map_cursor_world_y`（`0x69ccc`） |
| `+0x07` | `+0x30ca` | u8 | 恆寫 0，沒有人讀 | — |
| `+0x08` | `+0x30cb` | u8 | 恆寫 0，沒有人讀 | — |
| `+0x09` | `+0x30cc` | u8 | roster 人數 | `data_fdps_roster_member_count`（`0x64114`） |
| `+0x0a` | `+0x30cd` | i32 | 隊伍金錢 | `data_fdps_shared_party_total_gold`（`0x643a4`） |
| `+0x0e` | `+0x30d1` | u8 | 戰鬥動畫開關 | `data_fdps_ui_battle_animation_enabled`（`0x60010`） |
| `+0x0f` | `+0x30d2` | u8 | 地形資訊面板開關 | `data_fdps_ui_terrain_hud_user_enabled`（`0x60158`） |
| `+0x10` | `+0x30d3` | u8 | 音樂開關 | `data_fdps_audio_bgm_enabled_flag`（`0x60008`） |
| `+0x11` | `+0x30d4` | u8 | 音效開關 | `data_fdps_audio_sfx_enabled_flag`（`0x69d70`） |

四個座標在全域裡是世界像素，寫入時以帶號除法除以 24（一格的邊長）存成格數，讀回時乘 24。落進 `int` 全域的 u8 欄位（回合、單位數、章節、四個座標、roster 人數）讀回時是零延伸；四個開關的全域本身是 byte，原樣搬回。

`+0x0e` 起四個開關的順序是「動畫、地形面板」，與 slot 裡「地形面板、動畫」相反。

## slot（`+0x312b + n × 0xa28`）

寫入者一律是 `fdps_save_game_screen`，只寫玩家選定的那一個 slot。

| slot 內偏移 | 型別 | 內容 | 對應的全域 | 讀取者 |
| ---: | --- | --- | --- | --- |
| `+0x000` | `0xa00` | roster 的整塊複本（32 筆單位記錄，末筆的尾端被下面五個欄位蓋掉） | `data_fdps_roster_array_ptr` 指向的 roster | `fdps_load_game_screen`；`fdps_draw_save_slot_panel` 讀第 0 筆的肖像編號 `+0x07` 與等級 `+0x21` |
| `+0x9ec` | u32 | 酒館抽獎已抽旗標 | `data_fdps_bonus_lottery_drawn_flag`（`0x64110`） | `fdps_load_game_screen`，只在不處於村莊階段時寫回全域 |
| `+0x9f0` | u32 | 存檔時刻：分 | DOS `_dos_gettime` | `fdps_draw_save_slot_panel` |
| `+0x9f4` | u32 | 存檔時刻：時 | DOS `_dos_gettime` | `fdps_draw_save_slot_panel` |
| `+0x9f8` | u32 | 存檔日期：日 | DOS `_dos_getdate` | `fdps_draw_save_slot_panel` |
| `+0x9fc` | u32 | 存檔日期：月 | DOS `_dos_getdate` | `fdps_draw_save_slot_panel` |
| `+0xa00` | u8 | 章節索引；`0xff` 表示空 slot | `data_fdps_chapter_current_chapter_id` | `fdps_load_game_screen`、`fdps_saveload_screen_build`、`fdps_draw_save_slot_panel` |
| `+0xa01` | u8 | roster 人數 | `data_fdps_roster_member_count` | `fdps_load_game_screen` |
| `+0xa02` | i32 | 隊伍金錢 | `data_fdps_shared_party_total_gold` | `fdps_load_game_screen` |
| `+0xa06` | u8 | 地形資訊面板開關 | `data_fdps_ui_terrain_hud_user_enabled` | `fdps_load_game_screen` |
| `+0xa07` | u8 | 戰鬥動畫開關 | `data_fdps_ui_battle_animation_enabled` | `fdps_load_game_screen` |
| `+0xa08` | u8 | 音樂開關 | `data_fdps_audio_bgm_enabled_flag` | `fdps_load_game_screen` |
| `+0xa09` | u8 | 音效開關 | `data_fdps_audio_sfx_enabled_flag` | `fdps_load_game_screen` |
| `+0xa0a` | 30 byte | 沒有人寫也沒有人讀；slot 3 的最後 4 byte 是整檔的檢查碼 | — | — |

**五個 u32 欄位落在 roster 複本裡面**，不是接在它後面：寫入時先把 roster 的 `0xa00` byte 整塊搬進 slot，再把指標推到 `+0xa00`，往回在 `-0x04`、`-0x08`、`-0x0c`、`-0x10`、`-0x14` 依序寫月、日、時、分、抽獎旗標。被蓋掉的是 roster 第 31 筆（第 32 個成員）記錄的 `+0x3c`–`+0x4f`。讀檔時整塊 `0xa00` 搬回 roster，這五個 dword 也跟著回到第 31 筆記錄的尾端。所以 slot 實際保得住的成員是 31 筆，`struct fdps_save_slot` 把它寫成 31 筆 roster 加 60 byte 的空隙再接五個欄位。

時間欄位是 DOS 結構的 byte 零延伸成 dword；年份不存。版面上日期排成「日／月」、時間排成「時：分」，各兩位數，超過 99 會顯示成 `??`。

`fdps_draw_save_slot_panel` 印的「LV」是 roster 第 0 筆的等級，「MAP」是章節索引加 1（也就是章號），章節標題取 `FIELD.VFS` 的 `fdetxt%02d.txt`（章號）第 1 筆。

## 空 slot 與不會被寫到的 byte

章節 byte 等於 `0xff` 就是「沒有內容」：slot 的 `+0xa00` 決定存讀檔畫面把那格當空格（讀檔畫面不讓選、版面印文字 `0x209`），live-state 的 `+0x30c5` 決定標題畫面。除此之外沒有別的有效性標記，也沒有人看其他欄位。

檔案不存在時，兩條寫檔路徑與只讀的版面組裝各自造出一份初始映像，而且造法不同：

| 路徑 | 檔案不存在時的初始映像 |
| --- | --- |
| `fdps_save_game_screen`（以及只讀的 `fdps_saveload_screen_build`） | 整份映像 `memset` 成 `0xff`，**不經過解密**——這已經是明文。於是 4 個 slot 與 live-state 的章節 byte 全部是 `0xff` |
| `fdps_battle_system_submenu` | 只把 4 個 slot 的章節 byte（`+0x3b2b`、`+0x4553`、`+0x4f7b`、`+0x59a3`）寫成 `0xff`，其餘是 `malloc` 回來的原樣內容 |

之後的每次寫入都是讀進舊檔、只改自己的欄位，所以下列 byte 會一直保留檔案第一次被建立時的內容：`+0x30d5`–`+0x312a`、每個 slot 的 `+0xa0a` 之後、以及 slot 3 除了章節 byte 與檢查碼以外的全部。地圖單位陣列超過單位數的部分則是保留先前寫入者留下的內容（某次單位較多的戰鬥存檔、或建檔時的內容）。空 slot 除章節 byte 以外的部分也一樣。這些 byte 沒有意義，讀寫工具不應該解讀它們。

由此而來的外顯行為：

- **標題選單的第二項（讀 slot 的讀檔畫面）與第三項（讀 live-state 的繼續）共用同一個條件**：檢查碼相符**而且** live-state 的章節 byte 不是 `0xff`，兩個項目才一起解除反灰。檔案若是由存讀檔畫面從無到有建立、之後從未在戰鬥中存過檔，live-state 的章節 byte 是 `0xff`，標題選單就進不了讀檔畫面——即使 slot 裡有存檔。
- 檢查碼不符時標題畫面的兩個項目也一起反灰，雖然 slot 的讀取本身從不驗檢查碼。

## 檢查碼與加密

### 檢查碼（`fdps_compute_save_checksum`，`00056898`）

明文映像 `[0, 0x59c7)` 每個 byte 零延伸後加總，32-bit 繞回（這個長度下實際不會繞回），結果以 u32 存在 `+0x59c7`。範圍排除尾端 4 byte，也就是檢查碼欄位本身。呼叫端一律傳整檔長度 `0x59cb`，常式內部自己扣 4。

### 加密（`fdps_xor_crypt_buffer`，`000568b7`）

16-bit 密鑰，起始值 `0xa5`。每個 byte **先**更新密鑰、再拿密鑰的低 byte 與資料 XOR：

```
key = 0x00a5
for each byte b:
    key = (key + 0x9014) & 0xffff
    key = ((key << 3) | (key >> 13)) & 0xffff
    b ^= key & 0xff
```

起始值本身從不用來 XOR，密鑰流的前三個 byte 是 `cc 00 a1`。密鑰流只由位置決定、與資料無關，所以同一支常式既加密也解密，整個執行檔沒有另一支解密常式。加解密範圍是整份 `0x59cb` byte，包含檢查碼。

### 順序與誰驗

寫入一律是「填好明文 → 算檢查碼寫進 `+0x59c7` → 整份加密 → `fwrite`」；讀取一律是「`fread` → 整份解密 → 用明文」。四個讀取點裡只有兩個會比對檢查碼：

| function | 比對檢查碼 | 不符時 |
| --- | --- | --- |
| `fdps_title_screen` | 是 | 標題選單第二、三項保持反灰 |
| `fdps_load_savegame` | 是 | 開訊息窗印全域文字 `0x208`、不等按鍵就關窗，然後照樣載入 |
| `fdps_load_game_screen` | 否 | — |
| `fdps_saveload_screen_build` | 否 | — |

兩個寫入路徑讀舊檔時都不驗檢查碼，寫回時重算，所以一份檢查碼不符的檔被寫過一次之後就又是「相符」的。

### 不檢查的東西

沒有任何路徑檢查 `fread` 讀到的長度：短檔的尾端是 `malloc` 回來的原樣內容，一樣被解密、被使用。`fdps_load_savegame` 與 `fdps_load_game_screen` 不測 `fopen` 的結果，檔案不存在時會在 CRT 裡當掉——兩者的入口都已經先確認過檔案存在（系統選單的 `access`、標題畫面的反灰、讀檔畫面不讓選空 slot），正常遊玩碰不到。寫入路徑也不測寫入用的 `fopen`。

## 驗證

用 `tools/save_format/fde_sav.py verify` 對下列實際存檔依本文件的算法解密並重算檢查碼，全部與檔案內存的值相符，而且解密後再依本文件的順序封裝一次會得到逐 byte 相同的檔案：

| 存檔 | 存放的檢查碼 |
| --- | --- |
| 開發者遊戲目錄內的 `FDE.SAV` | `0x002dedc4` |
| 光碟 1 `PACK.VFS` 內的 `FDE.SAV`、`F30.SAV` | `0x001e3648` |
| 光碟 1、2 `PACK.VFS` 內的 `DEBUG.SAV` | `0x001e2b7b` |
| 光碟 2 `PACK.VFS` 內的 `FDE.SAV` | `0x001d9a28` |
| 光碟 2 `PACK.VFS` 內的 `F30.SAV` | `0x001e3648` |

開發者遊戲目錄內那份的欄位也與本文件的語意一致：live-state 是第 1 章（章節索引 0）第 10 回合、23 個地圖單位的戰鬥，地圖腳本區塊的前 703 byte 與 `MAP00.DAT`（703 byte）逐 byte 相同；slot 0 是章節索引 1、roster 1 人、900 金，存檔戳記 8 月 14 日 12:12，與該檔的修改時間一致；slot 1–3 的章節 byte 是 `0xff`、其餘是非 `0xff` 的雜訊，符合「檔案由戰鬥中存檔從無到有建立」的樣子。

`PACK.VFS` 內封存的是另一版遊戲，那幾份存檔用的是同一套檢查碼、加密與檢查碼位置；它們各欄位在那一版裡的語意不在本文件的範圍。
