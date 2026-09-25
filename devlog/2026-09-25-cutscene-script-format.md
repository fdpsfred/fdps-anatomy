# 票 25.2：過場腳本格式與解碼器

## 起點

票要的是：66 支 `ICONANI.VFS` 腳本都能解成步驟列表、知識庫有格式正典、腳本→地圖→文字區塊的對照給 25.6／25.8／25.14 用。硬指標是照 opcode 長度走完每一支都要剛好停在最後一個 byte 的結束指令上。

先查前作。`fd2-anatomy/tools/decoders/` 有 `fdfield_event_decoder.py` 之類，但 FD2 沒有位元組腳本這一層——它的章節事件是 `FDFIELD.DAT` 的表加上程式碼——所以沒有可沿用的解碼器。容器解析直接 import 本專案 `tools/vfs_dump/vfs_dump.py` 的 `parse_container`，照 `tools/_index.md` 的擁有者規則不複製。

語意的依據是 `src/icon.c`。直譯器與各 handler 在 emit 時已經把每個 opcode 的運算元位置、有號無號、回傳的新偏移都寫在註解裡，所以長度表是直接從 C 讀下來的，沒有再去 Ghidra 逐條對組語。

## 是否需要 workflow

考慮過這張票算不算「逐項判定」。結論是不算：解碼是確定性的位元組走訪，opcode 語意來自一支已經 emit 完的 function，呼叫端由掃 `src/` 的字面值與所在函式得出，沒有任何一步是在替某個 function 或 data 下身分、calling convention 或 emit 的判斷。因此沒有寫 workflow，全部是工具加測試。中途協調者也通知子 agent 沒有 Workflow 工具；這張票用不到。

## 第一次走訪

先在 `workspace/cutscene_script/probe.py` 寫了最小的長度表直接走 66 支。一次就全部停在最後一個 byte 的 `0x00`，剩餘 0。長度表因此不需要任何修正。`0x08 PLAY_WAV` 是唯一沒出現的 opcode。

一個插曲：原本把探測腳本放在 session 的 scratchpad，結果發現同一個 scratchpad 裡的 `probe.py` 被平行的存檔票蓋掉了（內容換成 `FDE.SAV` 的解密）。scratchpad 是平行 agent 共用的，之後一律把自己的中間檔放 `workspace/cutscene_script/`。

## 追蹤：文字區塊跟著地圖走

光解指令不夠，`DRAW_TEXT n` 的 `n` 指哪一個區塊要看執行當下。讀 `fdps_chapter_state_reset` → `fdps_build_map_unit_array` → `fdps_field_load_chapter_resources`：`SWITCH_MAP` 會把章節索引改掉然後整組重載，連 `FDETXT(m+1)` 一起。所以追蹤器記住「目前地圖」，每條文字引用都對到 `FDETXT(目前地圖+1)`，並驗證條目數在區塊範圍內——這個檢查若地圖追錯就會大量爆掉，實際 292 條 `DRAW_TEXT` 加 `0x63` 的 6 條全部落在範圍內，等於反向證實了追蹤模型。

`GOODEND` 是唯一需要「接續」的：它在 `fdps_chapter_30_end` 裡接在 `WIN29` 與片尾名單之後，中間不重載地圖。追蹤時讓它從 `WIN29` 的結束狀態開始。順手檢查片尾名單（`src/ending.c`）以章節索引 `0x1d`／`0x1a` 分支，而 `WIN29`、`WIN26` 都切出去又切回自己的地圖，所以那兩個比較看得到正確的章節索引。

## 單位編號：第一次模型太悲觀，第二次太樂觀的地方

第一版把勝利腳本的單位陣列當成完全未知，只有 `SWITCH_MAP` 之後才已知。後來確認戰鬥中單位只會被標退場、從不移除或重排（`fdps_relocate_unit_array` 是搬家不是縮），而陣列開頭永遠是「我方 slot + 波次 0」，所以改成「前段已知、尾段開放」。這讓 `WINGA26` 的 `0x61` 能直接標成我方 slot 3。

用開場腳本（陣列完全已知）一跑，冒出 5 處 `FACE_UNITS` 指到超出單位數的索引。第一反應是模型錯了——但逐條看步驟表：`ICON00` 在地圖 35 對單位 1、3 轉向，下一兩條才 `DEPLOY_WAVE` 把它們帶上來；`ICON11` 對單位 10 轉向，`0x19` 才部署；`ICON23` 的 38 號在 21 個單位時轉向，之後根本沒部署。`fdps_get_unit_record` 不檢查、`FACE_UNITS` 也不檢查，所以這是原版的越界寫入，寫的值 5 處都是 0。把這類改成「findings」而不是驗證失敗。

接著寫知識庫時原本寫了「有邊界檢查的三個 opcode 出貨腳本沒踩到邊界」，發文前用 JSON 查了一次才發現錯：`WIN24` 有 35 條 `RETIRE_UNIT`（地圖 22 上的 32–66 號，當時只有 32 個單位）和一條 `PLACE_UNIT 22`（地圖 59 上只有 2 個單位），全部被檢查略過。改寫並加了釘住這個清單的測試。兩者一起成了 pitfalls 的一列：`FACE_UNITS` 不能補檢查、三個有檢查的不能拿掉。

## `0x61`

`src/icon.c` 把它叫 `SCRIPT_OP_DEBUG_LEVEL_UNIT_3`。它只在 `WINGA26` 的 `0x15F`，在任何 `SWITCH_MAP` 之前，所以作用在第 27 章戰場的我方 slot 3（法蓮娜）。另外發現：`fdps_chapter_27_end` 的隱藏路線沒有名冊寫回，全專案 `fdps_roster_write_back_battle_units` 的呼叫者只有四個 end 檔和 `icon.c`，所以這條 opcode 的寫回是那條路線唯一的一次——拿掉它不只少了經驗，整場第 27 章的成果都不會記進名冊。寫進 pitfalls。巨集改名屬於 25.15，這裡沒動 `src/`。

## 其他順帶的發現

- `src/icon.c` 與 `src/icon.h` 的註解說 `fdps_icon_script_run` 有 67 個呼叫點；Ghidra 對 `0x21650` 的 xref 是 66，`src/` 掃出來也是 66（每支腳本恰好一個）。註解數錯了，留給 25.15。
- `ICON11` 的三選一是在地圖 40 上問的，所以問題與回應在 `FDETXT41` 的 `0x10`–`0x15`；`icon.h` 說的「loaded chapter's text block」字面上沒錯，但很容易被讀成 `FDETXT12`。
- 25.1 的 `tools/text_decode/` 在本票進行中出現了（尚未 commit），介面在它的 `_index.md` 寫得清楚，就加了一個延遲 import 的 `text_decode_renderer`（命令列 `--text`），沒有它時本工具照常運作。
- `resource_info/map.md` 原本有一句列舉「哪支腳本切到哪張地圖」，改成連到新文件，讓對照只有一個擁有者。這一行在 25.3 平行改寫 `map.md` 時被一起保留下來了。
