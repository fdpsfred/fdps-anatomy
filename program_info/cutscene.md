# 過場腳本直譯器

**驗證對象**：`FDPS.LE` 的過場腳本直譯器與它的 opcode 處理函式 `0x21650`–`0x222bf`、`0x22410`–`0x22743`（`src/icon.c`；中間的 `0x222c0` 是 `src/saf.c` 的 `fdps_saf_play_over_scene`，不在本頁範圍）；階段切換動畫、回合數橫幅、攻擊動畫與效果動畫 `0x1e840`、`0x1ea80`、`0x1eb00`、`0x1ef40`、`0x26e00`、`0x2a240`（`src/anim.c`）；畫面轉場 `0x2f410`、`0x2f6d0`、`0x2fb80`、`0x31780`（`src/transit.c`）。這些 function 在執行期做什麼，以本頁為唯一正典。腳本檔的位元組格式、opcode 長度、各腳本的播放時機表與地圖／文字區塊對照屬於 [`resource_info/cutscene_script.md`](../resource_info/cutscene_script.md)，本頁不重複。

## 誰播放腳本

唯一的入口是 `fdps_icon_script_run`（`0x21650`），參數只有 `ICONANI.VFS` 裡的成員名稱；容器名稱寫死在直譯器裡，呼叫端無法指定別的容器。全映像共 66 個呼叫點，全部是章節處理函式以字串字面值呼叫：

- 30 支章節 init 處理函式（`fdps_chapter_01_init`（`0x20e90`）起）各播一支 `ICONnn` 開場腳本；
- 30 支章節 end 處理函式（`fdps_chapter_01_end`（`0x3a410`）起）播 `WINnn`，第 18、27、30 章的 end 處理函式各有兩個呼叫點（`WIN17-1`、`WINGA26`、`GOODEND`）；
- 第 8 章的兩支事件處理函式播戰鬥中的三支：`fdps_chapter_08_event_for_turn`（`0x372d0`）兩處、`fdps_chapter_08_event_villagers_leave_cells`（`0x374e0`）一處。

每支腳本在什麼條件下播放見 [`resource_info/cutscene_script.md`](../resource_info/cutscene_script.md#66-支腳本與播放時機)；章節生命週期中 init／end 處理函式本身的呼叫時機見 [`chapter.md`](chapter.md)。檔名的 `nn` 是章節索引而不是章號，這條陷阱已在 [`rebuild_info/pitfalls.md`](../rebuild_info/pitfalls.md) 列出。

直譯器同步執行：呼叫端要等整支腳本跑完才拿回控制權，會畫場景的 opcode 都由自己呼叫 `fdps_render_view_frame`（`0x2beb0`）畫出每一幀並以 timer tick 節拍，`0x06` 的動畫自己以 tick 節拍，淡出淡入以垂直回掃加 `delay` 毫秒節拍，所以過場的速度不隨 CPU 速度改變。

## 直譯迴圈

`fdps_icon_script_run`（`0x21650`）的流程，依序：

1. 先呼叫 `fdps_units_clear_status_bit7`（`0x2db50`），清掉**每一個**單位狀態 byte（記錄 `+5`）的 bit 7——「本回合已行動」旗標——其他 bit 不動。這一步在開容器之前，連找不到腳本時也會執行。
2. 以 `fdps_vfs_open`（`0x39ab0`）開 `IconAni.vfs`。開不起來時印出 `file not found: 'IconAni.vfs'`、呼叫 `fdps_wait_any_key`（`0x567a0`）等一個鍵，然後**照樣**拿空 handle 往下讀成員——原版不在這裡返回。
3. 以 `fdps_vfs_load_file`（`0x39bd0`）把成員整個讀進記憶體。讀不到時等一個鍵，跳過整個迴圈直接收尾，一條 opcode 都不執行。
4. 迴圈：只要執行旗標（一個 byte）還是 1 就繼續。每一圈先呼叫 `fdps_flush_keyboard_queue`（`0x567b3`）清空鍵盤佇列、把地圖游標繪製模式 `data_fdps_map_cursor_draw_mode` 設成 0，再讀 `script[offset]` 分派。所以一條 opcode 期間按下的鍵不會被下一條讀到，任何 opcode 期間都不畫地圖游標。
5. 分派是一串無號比較（opcode 當 0..255 看）。opcode `0x00` 沒有自己的分支，它和所有不認得的值落在同一處：把執行旗標清成 0，回到迴圈頂端後離開。
6. 收尾：free 腳本、free 容器 handle、free 本支腳本 `0x08` 載入過的每一段 `.wav`，最後把游標繪製模式設成常數 1——不是還原成進入時的值。

腳本位置是一個 byte 位移 `offset`，每條 opcode 自己決定前進多少：多數在直譯器內 `offset += 長度`，五條（`0x01`、`0x02`、`0x05`、`0x09`、`0x10`）交給處理函式，由它回傳下一條的位移。單位記錄每條 opcode 都以 `fdps_get_unit_record`（`0x2d210`）重新取一次，因為 `0x11` 會重建整個單位陣列。

收尾時原版把目前章節索引與進入時的副本比較一次，但沒有任何分支使用結果，也沒有回傳值，不構成可觀察行為。

步驟 1 的清除是在**進入**時做，不是在離開時：腳本自己的 `0x62` 讓單位行動後會重新設上 bit 7，而直譯器不再清它。`fdps_chapter_15_init`（`0x21240`）在播完 `ICON14` 之後自己再呼叫一次清除，清掉 `ICON14` 以 `0x62` 讓光束砲座（陣營 0）行動時設上的 bit 7。少了那一次，第 1 回合我方階段一開始系統選單的「存檔」就是灰的——`fdps_battle_system_submenu`（`0x14ea0`）只要有任何未退場的單位帶 bit 7 就停用存檔，不分陣營——畫面上則看不出差別，因為肖像編號 `0x80` 的砲座沒有地圖單位圖，`fdps_draw_map_unit`（`0x2cda0`）遇到它時在讀旗標、畫任何東西之前就返回；它在敵方階段仍照常行動，因為 `fdps_battle_advance_turn`（`0x1e3f0`）在「敵方回合」字卡後本來就會再清一次。

## Opcode 的執行期效果

以下「單位」都是地圖單位陣列的索引，運算元都是無號 byte（`0x10` 的位移對除外），視野原點 `data_fdps_battle_view_window_origin_x`／`_y` 與游標座標 `data_fdps_map_cursor_world_x`／`_y` 以地圖像素計，一格 24（`0x18`）像素。

### 單位的走動、轉向、放置與退場

**`0x01` 走動**，`fdps_icon_script_walk_units`（`0x21f60`）。運算元：每小步停 F 幀、走 T 格、N 個單位，接著 N 組（單位, 方向）。

- 進入時把游標繪製模式與資訊欄旗標 `data_fdps_ui_play_active_flag` 都設 0，離開時兩者都設成常數 1（不是還原）。
- 迴圈由外而內：格 `0..T-1` → 小步 `1..6` → 停留幀 `0..F-1` → 單位 `0..N-1`。每個停留幀對每個單位：把腳本給的方向寫進記錄的方向欄；小步 1–5 把小步計數寫成該小步編號，小步 6 只在停留幀 0 那一次把小步計數清 0 並依方向把座標推一格（方向 0：`y+1`、1：`x-1`、2：`y-1`、其他值：`x+1`）。每個停留幀結束時畫一幀、再呼叫 `fdps_cycle_scene_palette`（`0x2eab0`）；後者有 tick 守門，同一 tick 內第二次呼叫不會讓調色盤循環變快。
- 所以一次走動共畫 `T × 6 × F` 幀，每格的座標只推進一次；`F = 0` 時一幀都不畫、單位也完全不動。
- 不看地形、不檢查佔位、不觸發格子事件、不捲動視野，也不檢查單位編號。

**`0x02` 轉向**，`fdps_icon_script_set_unit_facing`（`0x22100`）。運算元：停留 H 幀、N 個單位，接著 N 組（單位, 方向）。先把所有列出的單位的方向欄原樣寫入（不遮罩、不檢查範圍），**之後**才畫 H 幀；兩個迴圈是並列而不是巢狀，所以 `H = 0` 仍會轉向，所有單位在同一幀一起轉。游標模式與資訊欄旗標的處理同 `0x01`（進 0、出常數 1）。不檢查單位編號，出貨腳本因此有越界寫入，見 [`resource_info/cutscene_script.md`](../resource_info/cutscene_script.md#越界寫入)。

**`0x09` 閃爍退場**，`fdps_icon_script_blink_units_out`（`0x221e0`）。運算元：N，接著 N 個單位（每個 1 byte）。相位 `p = 1..7`，每個相位先把每個列出單位的**整個**狀態 byte 設成 `p & 1`，再畫 3 幀；共 7 相位 21 幀，速度不可調。第 7 相位寫 1，所以單位最後處於退場狀態。因為是整 byte 指派，退場以外的旗標（含「本回合已行動」）一併清成 0。游標模式與資訊欄旗標同 `0x01`。

**`0x0A` 放置**、**`0x0B` 退場**、**`0x0C` 復活**，三者都在 `fdps_icon_script_run`（`0x21650`）內，而且只有這三條先檢查「單位編號 < `data_fdps_map_unit_count`」（有號比較），不成立就整條略過、照樣前進：

- `0x0A`：把單位的 x、y、方向設成運算元。不動退場旗標，所以放置一個已退場的單位不會讓它出現。
- `0x0B`：狀態 byte `|= 1`，其他 bit 保留。
- `0x0C`：狀態 byte `&= 0xFE`，再把 6 個狀態計時（記錄 `+0x22..+0x27`）全清 0。HP、MP 不動。

**`0x12` 設狀態計時**：記錄 `+0x25 + i` 設成值，即 `status_timers[3 + i]`。單位編號與 `i` 都不檢查。

**`0x62` 單位行動一步**：以兩個運算元（單位、陣營選擇）呼叫 `fdps_map_actor_behavior_step`（`0x10010`），讓該單位照自己的 AI 行為走完一次行動，行動結束時會設上它的 bit 7。行為的內容見 [`map_ai.md`](map_ai.md)。

### 視野

**`0x05` 捲動到某格**，`fdps_icon_script_scroll_view_to_tile`（`0x21e30`）。以進入時的原點 `(ox, oy)` 計算：

```
tx = X * 24,  ty = Y * 24
ax = abs(tx - ox),  ay = abs(ty - oy)          // abs 是 CRT 呼叫
n  = (ax > ay ? ax : ay) / 24                  // 有號除法，向零截斷
若 n != 0：
    sx = (tx - ox) / n,  sy = (ty - oy) / n    // 有號除法，向零截斷，只算一次
    重複 n 次：ox += sx; oy += sy; 畫一幀
ox = tx, oy = ty
cursor_x = tx + 24, cursor_y = ty + 24
```

幀數由較長的一軸決定；每幀增量在迴圈前算好，較短一軸因截斷而落後的餘數在最後一步一次補上。距離不足一格時 `n = 0`，一幀都不畫，視野直接跳到目標。目標不對地圖邊界夾值。

**`0x0D` 直接設定視野**（在 `fdps_icon_script_run`（`0x21650`）內）：`ox = X * 24`、`oy = Y * 24`，游標 `= (ox + 24, oy + 24)`，不畫任何幀、不夾值。

**`0x10` 震動**，`fdps_icon_script_animate_view_offset`（`0x224f0`）。運算元：每步停 H 幀、S 步，接著 S 組 (dx, dy)。每個 byte 以無號讀入、大於 `0x7F` 時減 `0x100`，所以範圍是 −128..127。進入時存下原點 `(ox0, oy0)` 與資訊欄旗標，旗標設 0；每一步把原點設成 `(ox0 + dx, oy0 + dy)`——**相對進入時的原點**，不是相對上一步——然後畫 H 幀；結束時原點還原成 `(ox0, oy0)`、資訊欄旗標還原成進入時的值。這條不碰游標繪製模式，也是會動資訊欄旗標的 opcode 裡唯一「還原」而非「設成 1」的（`0x01`、`0x02`、`0x09` 都設成 1，`0x05`、`0x0D` 不碰它）。`H = 0` 時原點被改寫又還原，中間沒有任何一幀。

### 淡入淡出與調色盤

三者都以主調色盤 `data_fdps_vga_main_palette_ptr` 為來源，經 `fdps_set_palette_range`（`0x22f40`）重傳 DAC 第 0–255 號；那個 function 對每個分量算 `clamp(master + bias, 0, 63)`（有號夾值）。每一步都從主調色盤重算，不累積、也不繼承 DAC 上當下的顏色。

- **`0x0E` 淡出**，`fdps_icon_script_fade_to_black`（`0x22410`）：`d = 0, 4, …, 60`（16 步），每步先等垂直回掃**開始**，上傳 `bias = -d`（三個分量相同），再 `delay(運算元)` 毫秒。最暗一步是 −60，所以分量大於 60 的顏色停在 1–3，畫面是極暗而非全黑。
- **`0x0F` 淡入**，`fdps_icon_script_fade_in`（`0x22480`）：`d = 64, 60, …, 0`（17 步），其餘同上。第一步 −64 必定全黑，最後一步 0 恰好是主調色盤本身。兩者刻意不對稱，淡入比淡出多一步。
- **`0x15` 調色盤偏移**（在 `fdps_icon_script_run`（`0x21650`）內）：三個運算元分別是 R、G、B 的 bias，以無號 byte 讀入，只能變亮；一次上傳、不畫幀。64 使全畫面變白，0 還原主調色盤。

### 文字與三選一

**`0x03` 顯示文字**：呼叫 `fdps_draw_text`（`0x1ff60`），文字來源是**目前載入**的章節文字區塊 `data_fdps_current_chapter_text_ptr` 的第 n 條，筆的起點是畫面 `0xA0000`、行距 `0x140`，顏色固定為前景 `0xD0`、背景 0、外框 `0x6D`，腳本無法指定。條目裡的說話者代碼自己開訊息視窗並把筆移到視窗內，條目結束時等待按鍵並關窗；這些行為屬於 [`dialog.md`](dialog.md)。目前文字區塊隨 `0x11` 改變，見下。

**`0x63` 三選一**，`fdps_icon_script_prompt_three_way_choice`（`0x22600`），不讀任何運算元。以目前文字區塊的 `0x10`–`0x15` 六條、FACE.CEL 第 122 號頭像，問兩道二選一（`fdps_prompt_two_choice`（`0x17990`））：

```
branch = 3
開窗、畫 0x10、answer = 二選一、關窗
若 answer == 0（左）：畫 0x11；branch = 1
否則：開窗、畫 0x13、answer = 二選一、關窗
      若 answer == 0：畫 0x14；branch = 2
      否則：畫 0x15
畫 0x12
回傳 branch
```

兩道題都只把 0（左）當作選中；右選項 1 與取消 −1 一律走「否則」，所以沒有不選的出口，連按兩次取消得到 3。兩道問題畫在剛開的視窗裡（筆在 (138, 131)），其餘四條從畫面原點畫起、由條目自己的說話者代碼開窗。直譯器把回傳值存著，給之後的 `0x04 0xFF` 用；這個存放處沒有初值，沒問過就用會拿到堆疊殘值。

### 地圖與部署

**`0x11` 切換地圖**：把章節索引 `data_fdps_chapter_current_chapter_id` 設成運算元，然後呼叫 `fdps_chapter_state_reset`（`0x22750`）。重設的內容（重載地圖資源與文字區塊、重建單位陣列並部署波次 0、清格子事件旗標、視野與游標歸零、回合數設 1）屬於 [`chapter.md`](chapter.md)；對腳本而言，之後的 `0x03` 讀新地圖的文字區塊、`0x04` 部署新地圖的記錄、單位編號指向新陣列。腳本本身與容器 handle 不受影響，照樣往下執行。

**`0x04` 部署波次**：呼叫 `fdps_deploy_wave`（`0x23830`），參數是（目前章節索引, 波次, 放置方式）；波次運算元為 `0xFF` 時改用 `0x63` 存下的答案。部署規則見 [`chapter.md`](chapter.md)。

**`0x13` 觸發格子事件**：`data_fdps_map_cell_event_triggered_flags[e] = v`（`e` 為 0..255，不檢查表長 `0x20`），接著呼叫 `fdps_map_apply_triggered_cell_changes`（`0x2e910`）把所有「旗標非 0 的事件碼」所在、且屬性類別為寶箱（`0x20`）或重繪格（`0x60`）的格子圖磚編號加 1、事件碼清 0。圖層格式見 [`resource_info/terrain.md`](../resource_info/terrain.md)。

**`0x14` 改寫圖磚**：地形層 0 的格 `(x, y)` 位於 `層 + 0x0B + (x + stride * y) * 2`，`stride` 是層頭 `+7` 的有號 word；把運算元的兩個 byte 以一次 16-bit 寫入。座標不檢查。

### 聲光媒體

**`0x06` 播放動畫**：`sprintf("Icon%04d.saf", n)`，以 `fdps_vfs_load_file_or_exit`（`0x29400`）從 `IconAni.vfs` 讀出，交給 `fdps_saf_play_over_scene`（`0x222c0`）同步播完後立即 free。播放期間每個 tick 都重畫場景底圖並疊上該幀，每幀停留幀記錄上的時長（有號 word，≤ 0 的幀不顯示），不畫資訊欄、不做調色盤循環。格式見 [`resource_info/saf.md`](../resource_info/saf.md)。

**`0x07` 切換音樂**：運算元 `0xFF` 改成 −1（停止），其他值原樣交給 `fdps_cd_set_music_track`（`0x30bf0`）。音樂編號到 CD 音軌的對應與「音樂關閉」設定的處理見 [`cd_audio.md`](cd_audio.md)。

**`0x08` 播放音效**：先 `sprintf("Icon%04d.wav", n)`，已載入的段數小於 20 時才讀出並以 `fdps_audio_start_wav`（`0x30a00`）播放一次（取樣率與音量取自檔頭），緩衝區留到腳本結束才 free；第 21 段起只做 `sprintf`。出貨腳本沒有任何一條 `0x08`，容器裡也沒有 `.wav`。

### `0x61`：第 27 章隱藏路線的經驗與寫回

**`0x61`**（在 `fdps_icon_script_run`（`0x21650`）內，無運算元）：重複 10 次「`data_fdps_battle_pending_xp_credit = 99`；`fdps_unit_award_exp_and_level_up(3)`」，每輪都重新設定待發經驗，因為發放會把它用掉；之後呼叫 `fdps_roster_write_back_battle_units`（`0x23980`）把整個單位陣列寫回名冊。經驗與升級的算法見 [`battle.md`](battle.md)。這條 opcode 只出現在 `WINGA26`，是該路線唯一的名冊寫回，不能當作除錯碼拿掉——這條陷阱已列在 [`rebuild_info/pitfalls.md`](../rebuild_info/pitfalls.md)，事實擁有者是 [`resource_info/cutscene_script.md`](../resource_info/cutscene_script.md#單位編號)。

## 戰鬥中播放腳本會讓已行動的單位重新可動

`fdps_icon_script_run`（`0x21650`）進入時的 bit 7 清除（見〈直譯迴圈〉）對所有單位生效，不分陣營、不管單位有沒有在過場中出場。開場與勝利腳本在戰鬥外播放，沒有影響。戰鬥中播放的只有第 8 章的三支，效果取決於播放時所在的階段迴圈；各階段迴圈的「可行動」測試見 [`battle.md`](battle.md)。

- **`ICON7-1`、`ICON7-2`**：由 `fdps_chapter_08_event_for_turn`（`0x372d0`）在陣營 0 的回合事件中播放（回合事件表見 [`resource_info/map.md`](../resource_info/map.md)）。`fdps_battle_advance_turn`（`0x1e3f0`）在「敵方回合」字卡之後先清一次 bit 7、緊接著才跑陣營 0 的回合事件，中間沒有任何單位行動，所以這兩次清除沒有可觀察的效果。
- **`ICON7-3`**：由牢門格的格子事件 `fdps_chapter_08_event_villagers_leave_cells`（`0x374e0`）播放。這支處理函式不看觸發者，任何陣營的單位走上牢門格都會觸發，一章一次：
  - **友軍階段**（客將費塔加自己開門的正常流程）：`fdps_battle_npc_turn_phase`（`0x12b20`）只由小到大掃一遍，已掃過的索引不再回頭，還沒掃到的友軍本來就未行動，所以誰能行動不變。唯一的差別是本回合已行動的我方單位在這個階段剩下的時間改畫成未行動的影格，直到「敵方回合」字卡後的清除為止都一樣。
  - **敵方階段**：`fdps_battle_enemy_turn_phase`（`0x12960`）的兩遍都以 `& 0x81` 判定可行動。敵人在第一遍觸發時，第一遍已行動的敵人全部會在第二遍再行動一次；在第二遍的索引 i 觸發時，索引大於 i、第一遍已行動的敵人會在第二遍再行動一次。
  - **我方單位回合**：`fdps_battle_unit_turn`（`0x15470`）先標已行動、後派送格子事件，於是本回合已行動的我方單位（含踩上牢門的那一個）都能再行動一次。
  - **全軍前進**：`fdps_battle_system_menu`（`0x14ab0`）逐一移動時先派送事件、後標已行動。在索引 i 觸發後，索引小於 i 的我方單位 bit 7 都被清掉、不再回頭；索引大於 i、本回合稍早已行動過的我方單位通過 `& 0x85` 測試而被再移動一次；接著的回合推進把 bit 7 為 0 的我方單位算成閒置而讓它們休息回復 HP。

後三項是原版 bug，機制見 [`known_bugs.md`](known_bugs.md)。

## 階段切換動畫與回合數橫幅

`fdps_play_vfs_animation`（`0x1eb00`）播放 `MISC.VFS` 裡的一段全螢幕動畫。呼叫端是 `fdps_battle_advance_turn`（`0x1e3f0`）的敵方階段 `EnyPhase.saf` 與玩家階段 `PlyPhase.saf`，以及 `fdps_load_savegame`（`0x23e20`）讀檔後的 `PlyPhase.saf`。流程：

1. 以 `fdps_vfs_load_entry`（`0x2a140`）讀出成員；把畫面分別複製兩份（背景、原畫），兩份都直接從 `0xA0000` 讀。
2. 變暗：level `1..5`，每步以 `fdps_blit_tint_rect`（`0x30010`）把原畫面以陰影表 level、色調 0 寫到畫面，等回掃開始、等回掃結束、等 tick 前進。
3. 把變暗後的畫面再抓一次成為背景。名稱等於 `PLYPHASE.SAF` 時先播回合數橫幅。比較之所以會成立，是因為讀成員時 VFS 已把呼叫端的字串就地轉成大寫（[`rebuild_info/pitfalls.md`](../rebuild_info/pitfalls.md) 的 VFS 大小寫一列）。
4. 以 `fdps_saf_play_over_background`（`0x1ecf0`）在背景上播完動畫。
5. 還原：level `5..1`（不到 0），每步從未變暗的那一份原畫計算，節拍同步驟 2。

**回合數橫幅** `fdps_animate_turn_banner`（`0x1e840`）：在 360×240 的暫存頁上合成、每步把 312×192 的視窗貼到畫面 (4, 4)。滑入 13 步，位置表 `t = {-60, -20, 10, 35, 50, 65, 75, 80, 85, 90, 92, 91, 90}`，「回合」字樣畫在 `x = t + 0x14`、數字畫在 `x = 0x12C - t`，兩者 y 都是 `0x5C`；每步等一個 tick。停留 `delay(500)` 毫秒。滑出用同一張表的第 **10** 項倒數到第 0 項（11 步），所以開始離場的那一步先從 90 跳回 92。橫幅圖從常駐的 `BaseAni.vfs` 映像以 `fdps_baseani_get_entry_or_exit`（`0x2a240`）取得，找不到時印出 `File not found: %s` 並 `exit(1)`。

**回合數字** `fdps_draw_turn_number`（`0x1ea80`）：`sprintf("%d", 回合數)`，每個字元 `c` 畫圖表第 `c - 0x2F` 項（`'0'` 是第 1 項，第 0 項是「回合」字樣），x 每位數加 `0x1C`。

## 攻擊與效果動畫

**攻擊動畫** `fdps_play_attack_animation`（`0x1ef40`），由 `fdps_unit_attack_target`（`0x1c3a0`）呼叫，播放常駐 `BaseAni.vfs` 的 `EasyAni.Saf`（找不到時得到空指標，不結束程式）：

- 動畫原點以**防守方**的格子計算：`x = pos_x * 24 - ox`、`y = pos_y * 24 - oy - 6`，座標 byte 無號擴展。
- 防守方一定畫 HP 條；攻擊方只在 `fdps_check_can_counter_attack`（`0x137e0`）回傳 1 時畫。條的圖案依記錄 `+6` 陣營 byte：0 用第 2 號，其他用第 1 號。
- 填滿格數（共 41 格）：`hp_max <= 0` 時 0，否則 `(hp_cur * 41 + hp_max - 1) / hp_max`（有號除法），即向上取整。HP 在動畫開始前讀一次，播放中不變。
- 幀數是動畫的總幀數，每幀固定一個 tick，不看幀記錄的時長；每幀畫場景、HP 條、動畫幀（帶音效旗標），等回掃開始與結束後貼上視窗，再等 tick。

**效果動畫** `fdps_play_vfs_animation_over_units`（`0x26e00`）：法術、道具、回復、狀態結算、第 30 章不死者復活與 AI 行動會以一份單位清單呼叫它，從 `MISC.VFS` 讀出動畫，在每個列出單位上同時播放：

- 每幀固定停 **2** 個 tick，不看幀記錄的時長。
- 每個單位的原點 `x = pos_x * 24 - ox - 0x18`、`y = pos_y * 24 - oy - 0x1E`。
- 幀上的音效只在第一個單位、每幀的第一個 tick 觸發一次。
- 畫面外的單位不做剔除，由逐格的繪製測試擋掉。

## 畫面轉場

**縮放** `fdps_transition_zoom`（`0x31780`）：村莊選單進出各場所、以及酒館、教會、道具、武器店、秘密商店的進出場都用它（見 [`village.md`](village.md)）。九步，`pass = 0..8`；`zoom_out` 非 0 時 `step = pass`（由放大收到 1:1，進場），為 0 時 `step = 8 - pass`（由 1:1 放大，離場）。每步：

```
等回掃開始
bias = -(8 - step) * 3              // 第 0 步 -24，第 8 步 0
上傳主調色盤 + bias
等回掃結束
cx = (center_x - (center_x - 0x9F) * step / 8) * 4   // /8 為有號除法，向零截斷
cy = (center_y - (center_y - 0x63) * step / 8) * 4   // 單位是 1/4 像素
step == 8 ？ 整張圖 memmove 到畫面
          ： 以鏡頭高度 {50,200,500,800,1000,1200,1300,1400,1500}[step] 旋轉縮放貼圖
等 tick 前進
```

離場方向結束後把畫面清成 0；兩個方向最後都以 bias 0 重傳主調色盤。

**隨機方塊** `fdps_transition_random_blocks`（`0x2fb80`）：存檔與讀檔畫面的進出場（`fdps_save_game_screen`（`0x241e0`）、`fdps_load_game_screen`（`0x24490`）），參數固定為 320×200、方塊 4×3、16×16 的相位格、每格停 1 毫秒。

- 相位格表以列優先填入後洗牌：對 `i = 0..255` 依序抽 `r = rand() % rows`、`c = rand() % cols`，交換第 `i` 項與第 `c + cols * r` 項。一次轉場呼叫 **512 次** `rand()`；遊戲共用同一個從不 `srand` 的 CRT 亂數，存讀檔畫面進場與離場各做一次轉場，所以每開關一次存讀檔畫面，轉場本身就讓之後戰鬥中的亂數序列多位移 1024 步（亂數在戰鬥中的用途見 [`battle.md`](battle.md)）。
- 每一相位格畫出它在整張畫面上的所有方塊：`blocks_x = ceil(80 / 16) = 5`、`blocks_y = ceil(66 / 16) = 5`；方塊列的像素列是 `3 * (row_band * 16 + cell_row)`，行是 `cell_col + col_band * 16`；列超過 200 的方塊不畫，行的測試拿方塊索引和像素寬比較而幾乎不生效。最後一條方塊列從第 198 列起畫 3 列，多寫一列到可見畫面之下的 VGA 記憶體，看不出來。
- 每畫完一個相位格 `delay(1)`。

**方框與滑動** `fdps_transition_box`（`0x2f410`）與 `fdps_transition_slide`（`0x2f6d0`，八種樣式）在映像中沒有任何呼叫點或參照，遊戲走不到；見 [`../cut_content/code.md`](../cut_content/code.md) 的 C6。

## 相關文件

- [`resource_info/cutscene_script.md`](../resource_info/cutscene_script.md)：腳本格式、opcode 長度表、66 支腳本的播放時機與地圖／文字區塊對照、越界寫入的實例
- [`chapter.md`](chapter.md)：章節 init／end 處理函式、章節狀態重設與波次部署
- [`dialog.md`](dialog.md)：訊息視窗、說話者代碼與二選一提示
- [`cd_audio.md`](cd_audio.md)：音樂編號與 CD 音軌
- [`known_bugs.md`](known_bugs.md)：原版 bug 的機制
- [`rebuild_info/pitfalls.md`](../rebuild_info/pitfalls.md)：未初始化的 tick latch、`volatile` 的 tick 計數器、`0x61`、`FACE_UNITS` 越界等重建陷阱
- [`../cut_content/_index.md`](../cut_content/_index.md)：遊戲走不到的內容
