# 對話系統

**驗證對象**：`FDPS.LE` 的訊息視窗與文字繪製——頭像載入 `0x177d0`、二選一提示 `0x17990`、等待按鍵 `0x203d0`、開窗 `0x205b0`、收窗 `0x20820`、從說話者格子開窗 `0x20a70`（`src/msgwin.c`）；文字直譯器 `0x1ff60`、字模繪製 `0x1fd80`、`0x1fed0`、數字欄位 `0x17530`（`src/text.c`）。這些 function 在執行期做什麼，以本頁為唯一正典。文字區塊的位元組格式、token 表與 `FDETXT.FON` 的佈局屬於 [`resource_info/text.md`](../resource_info/text.md)，`.CEL` 的格式與各圖表的 sprite 數屬於 [`resource_info/cel.md`](../resource_info/cel.md)，本頁不重複。

## 畫面上的位置與素材

訊息視窗的每個元件都畫在寫死的位置上。座標是 mode 13h 畫面的 (x, y)，每列 320（`0x140`）byte：

| 元件 | 素材 | 位置 | 由誰畫 |
| --- | --- | --- | --- |
| 訊息框 | `Message.cel` sprite 0，302×73 | (9, 120)，占 120..192 列 | `fdps_message_window_open`（`0x205b0`）、`fdps_message_window_open_from_tile`（`0x20a70`） |
| 頭像 | `FACE.CEL` 的一筆，125×100 | (12, 90)，上緣高出訊息框 30 列 | `fdps_load_and_draw_portrait`（`0x177d0`） |
| 戰場上的文字原點 | `FDETXT.FON` 16×16 字模 | (138, 131)，即位址 `0xaa44a` | `fdps_draw_text`（`0x1ff60`） |
| 村莊裡的文字原點 | 同上 | (20, 131)，即位址 `0xaa3d4` | `fdps_draw_text`（`0x1ff60`） |
| 等待指示 | `Command.cel` sprite `0x48`..`0x4b` | (280, 166)，訊息框右下角 | `fdps_message_window_wait_key`（`0x203d0`） |
| 二選一的兩格 | `Shadow.cel` sprite 4..13，24×24 | 左格 (229, 103)、右格 (255, 103) | `fdps_prompt_two_choice`（`0x17990`） |

`Message.cel`、`Command.cel`、`Shadow.cel`、`Number.cel` 由 `fdps_load_global_resources`（`0x29660`）在啟動時從 `MISC.VFS` 載入並常駐；`FACE.CEL` 不在容器裡，每次要畫頭像才以 `fopen("FACE.CEL", "rb")` 從目前目錄開檔（見下節）。

等待、提示與開關窗動畫每一幀只把**視野區**呈現到畫面上：(4, 4) 起 312×192 的矩形。它們都先在一張 360×240 的合成頁上組好整幀，再在兩段式的垂直歸線等待之後把視野區複製上去；合成頁上的像素座標等於畫面座標加 20。戰場上的背景每幀由 `fdps_draw_scene_layers`（`0x2bf60`）重新合成，村莊裡（`data_fdps_village_mode_flag` 非 0）則把目前畫面上的視野區原樣複製回來當背景。

所有等待都以遊戲時鐘為節拍：tick 計數器 `data_fdps_timer_tick_counter` 只由 `fdps_timer_tick_handler`（`0x30790`）遞增，而 `fdps_audio_init`（`0x304e0`）經 `fdps_audio_timer_install`（`0x307b0`）把這個中斷設成每秒 25 次。本頁的「tick」都是 1/25 秒。

## 文字的來源：文字區塊與條目

`fdps_draw_text`（`0x1ff60`）的參數是（文字區塊、條目編號、畫筆位址、pitch、前景色、底色、陰影色），回傳最後的畫筆位址。呼叫端交給它的文字區塊有兩個常駐的，外加一個臨時載入的——存讀檔畫面的 slot 版面 `fdps_draw_save_slot_panel`（`0x24a40`）自己從 `FIELD.VFS` 載入該 slot 章節的 `fdetxt%02d.txt`、畫第 1 筆（章名）後立刻釋放（見 [`save.md`](save.md)）。兩個常駐的是：

- `data_fdps_current_chapter_text_ptr`：本章的 `FDETXTnn.TXT`，`nn` 是章號（章節索引加 1）。戰場階段由 `fdps_field_load_chapter_resources`（`0x227e0`）載入，村莊階段由 `fdps_load_field_chapter_resources`（`0x31540`）重載；過場腳本切換地圖時會換成別的區塊，見 [`cutscene.md`](cutscene.md) 與 [`resource_info/cutscene_script.md`](../resource_info/cutscene_script.md)。
- `data_fdps_all_game_text_ptr`：全域文字 `FDETXT00.TXT`，`fdps_load_global_resources`（`0x29660`）啟動時載入。選單、商店、撿到物品、升級等系統訊息都在這裡。

條目的定位是 `entry = block + (short) block[text_id * 2]`：以 `text_id * 2` 讀一個**帶號** 16-bit 的 byte 位移，加回**區塊起點**。之後逐個 16-bit token 讀下去，直到讀到 -1。

一般的呼叫端傳入的三色都是前景 `0xd0`、底色 0、陰影 `0x6d`。

## 文字的繪製

`fdps_draw_text`（`0x1ff60`）逐 token 處理，畫筆（cursor）從 `dest` 起步，另有一個行數計數從 0 起：

| token | 動作 |
| --- | --- |
| `>= 0` | 以 `fdps_draw_glyph`（`0x1fd80`）畫字模 token，畫筆右移 `data_fdps_glyph_advance_x`（16） |
| -2 換行 | 行數加 1，畫筆 = `dest + pitch * line_height * 行數`；`line_height` 在進入時取自 `data_fdps_font_line_height`（18） |
| -3 換頁 | 清空鍵盤佇列、等待按鍵、重畫訊息框、畫筆回到文字原點（見「換頁」節） |
| -4／-5 代入 | 遞迴呼叫自己，畫全域文字的第 `data_fdps_dialog_last_action_text_id_param`／`data_fdps_dialog_subst_text_id_2` 筆，畫筆接在後面 |
| -6 數字 | `sprintf("%d", data_fdps_dialog_last_action_value_param)`，每個字元以「字元 − `'0'`」當字模索引畫出，每位右移 16 |
| -0x11 n | 換說話者，n 是角色編號 |
| -0x12 n | 換說話者，n 是地圖單位索引 |

換行的原點是 `dest` 不是目前的畫筆，步距是「行數 × 行高」而不是「目前位置加一行」，所以接在半行後面的呼叫端換行後也回到左邊界。行數與寬度都不檢查：戰場原點 (138, 131) 起每行 18 列，第 1..3 行落在 131、149、167 列，第 4 行落在 185 列，超出訊息框下緣（192 列）；一行放 10 個字模（138..297 行），第 11 個字模（298..313 行）壓到框的右緣（310 行），第 12 個字模起越過畫面右緣，x ≥ 320 的像素落到下一列的左邊。

出貨文字裡帶說話者碼、超出這個範圍的條目共九筆，其中四筆會被畫出，每筆都是某一行剛好 11 個字，第 11 個字越過框的右緣 3 像素：

| 條目 | 超出 | 由誰畫 |
| --- | --- | --- |
| `FDETXT05` `0x09` | 一行 11 字 | 第 5 章開場腳本 `ICON04` |
| `FDETXT18` `0x10` | 一行 11 字 | 第 18 章開場腳本 `ICON17` |
| `FDETXT46` `0x0a` | 一行 11 字 | 第 18 章勝利腳本 `WIN17-1`（勇者徽章） |
| `FDETXT24` `0x19` | 一行 11 字 | `fdps_chapter_24_post_action`（`0x3b3d0`）在決鬥獲勝時 |

另外五筆沒有任何程式路徑會畫：`FDETXT20` `0x0c`（一頁四行）、`FDETXT10` `0x0c`（一行 13 字，第 12 個字起越過畫面右緣）、`FDETXT25` `0x0e`（一行 11 字）、`FDETXT30` `0x1e` 與 `FDETXT64` `0x20`（一行 16 字）。判定的依據是把章節文字區塊的條目交給 `fdps_draw_text`（`0x1ff60`）的全部來源：過場腳本的文字引用（[`resource_info/cutscene_script.md`](../resource_info/cutscene_script.md) 的對照表）、章節處理函式寫死的條目編號、死亡腳本 opcode 3 以上的運算元（[`resource_info/map.md`](../resource_info/map.md)）、村莊的第 4–8 筆與勝敗條件視窗的第 2、3 筆，以及片尾字幕 `fdps_play_ending_credit_roll`（`0x1ba40`）逐人讀的條目（章節索引為 `0x1d` 時從目前區塊的 `0x20` 起，否則從 `0x19` 起）。`FDETXT30` 的 `0x15`–`0x1f` 與 `FDETXT64` 的 `0x17`–`0x21` 是逐 token 相同的同一段台詞，兩處都沒有人引用；第 30 章結束時 `WIN29` 從地圖 63 切回地圖 29，片尾字幕讀的是 `FDETXT30` 的 `0x20` 起。

代入碼（-4、-5）的遞迴呼叫固定用全域文字、固定用 `0xd0`／0／`0x6d` 三色，**不沿用**呼叫端傳入的顏色與區塊；pitch 與畫筆則沿用。代入的內容由呼叫端在呼叫前寫進兩個全域（例如商店把角色名寫進第一個、物品名寫進第二個）。

數字碼（-6）的字距是寫死的 16，不讀 `data_fdps_glyph_advance_x`。它不經過字模對照表，所以字模表的 0..9 必須是數字字形（[`resource_info/text.md`](../resource_info/text.md)）；值為負時 `'-'` 變成字模索引 −3，讀到字模表前方 `0x60` byte 的記憶體，這條陷阱已列在 [`rebuild_info/pitfalls.md`](../rebuild_info/pitfalls.md)。出貨的遊戲走不到這一步：寫入 `data_fdps_dialog_last_action_value_param` 的只有下列七處，畫文字時的值都不為負——

- 搜尋到的金錢 `fdps_battle_search_cell_at_cursor`（`0x184f0`）：可搜尋格記錄的帶號金額，出貨資料沒有負值；
- 死亡腳本的金錢 `fdps_run_death_scripts`（`0x1d990`）：帶號運算元，出貨資料是 180–5,000；
- 商店買入 `fdps_shop_buy_loop`（`0x33b80`）：無號的售價；扣掉舊裝備的折抵之後若 ≤ 0，先取負號再畫「退款」訊息；
- 賣出 `fdps_village_item_sell_loop`（`0x34000`）：無號售價的折算；
- 轉職 `fdps_church_promote_loop`（`0x345a0`）：無號 byte 的成長值；
- 升級 `fdps_level_up_apply_stat_gain`（`0x1e370`）與中毒 `fdps_battle_tick_status_effects`（`0x1fa30`）：值不為負，而且它們以 `fdps_draw_number`（`0x17530`）或數字指示畫出，不經過 -6。

### 字模

`fdps_draw_glyph`（`0x1fd80`）畫一個字模，字模的 bitmap 位址是 `font + index * data_fdps_font_glyph_stride_bytes`（32）。字格的寬高是 `data_fdps_font_glyph_width`／`data_fdps_glyph_cell_height`（都是 16，以零延伸的 byte 讀）。這八個字型全域只由 `fdps_load_global_resources`（`0x29660`）在啟動時寫入一次，之後沒有人改。三種顏色的 0 各有意思：

1. 底色非 0：先以底色填滿 16×16 字格。對話一律傳 0，所以字底下透出訊息框。
2. 裝飾：外框旗標 `data_fdps_font_outline_enabled_flag` 非 0 時，以陰影色在上下左右四個相鄰位置各畫一次字模（**不看陰影色是否為 0**）；旗標為 0 時，陰影色非 0 才在 `(+data_fdps_font_shadow_offset_x, +data_fdps_glyph_shadow_row_offset)` = (+1, +1) 畫一次。旗標啟動時寫成 0 而且沒有人改，所以遊戲裡的字一律是右下 1 像素的陰影、外框分支從不執行。
3. 前景色非 0：以前景色畫字模本體。

`fdps_blit_glyph_1bpp`（`0x1fed0`）是實際的 1bpp 繪製：每列從新的 byte 開始、最高位元在左，位元為 1 才寫入，位元為 0 什麼都不寫。

## 換頁

-3 在 `fdps_draw_text`（`0x1ff60`）內依序做：

1. `fdps_flush_keyboard_queue`（`0x567b3`）清掉佇列裡已有的按鍵，所以前一頁期間按的鍵不會直接翻過這一頁；
2. `fdps_message_window_wait_key(1, 100)`：顯示等待指示，最多 100 輪（見「等待按鍵」節）；
3. 重畫訊息框：戰場上重新合成場景，把 `Message.cel` sprite 0 畫在 (9, 120)；村莊裡複製畫面上的視野區，再把村莊的視窗圖 `ShopWin.Cel` 的 sprite 0 畫在 (4, 121)。兩者都是不透明的模式 0 繪製，框內的舊字被蓋掉。頭像緩衝區還在的話，把頭像再畫在 (12, 90)。兩段式歸線等待後把視野區呈現上去；
4. 行數歸 0，**`dest` 參數本身**被改寫成文字原點——戰場 `0xaa44a`、村莊 `0xaa3d4`——畫筆回到那裡。之後的換行都以新的 `dest` 起算。

所以一頁結束的樣子是：字消失、空白的框留在畫面上。呼叫端自己開窗、畫一筆以 -3 結尾的條目、再收窗時，這個 -3 就是那段話唯一的等待。

## 換說話者

兩個說話者碼都在 `fdps_draw_text`（`0x1ff60`）內處理，它們讓一筆條目自己開關視窗：

- 這筆條目裡已經開過窗（區域旗標）時，先 `fdps_message_window_wait_key(1, 100)` 再 `fdps_message_window_close`（`0x20820`）——**這裡不清鍵盤佇列**，與換頁不同；
- 決定頭像編號與說話者的格子，呼叫 `fdps_message_window_open_from_tile`（`0x20a70`）；
- 行數歸 0，`dest` 改寫成 `0xaa44a`（不分村莊或戰場），畫筆回到那裡，token 前進兩個 word，開窗旗標設 1。

條目讀到 -1 結束時，開過窗就再等一次按鍵（同樣不清佇列）並收窗。一筆條目沒有說話者碼時，它不開也不關任何視窗。

**-0x11（角色編號）**：以 `fdps_battle_find_unit_by_character_id`（`0x2dc20`）找地圖上角色編號（記錄 `+8`）相符、未退場的第一個單位。頭像編號先設成角色編號本身；找的過程中每遇到一個角色編號相符的記錄（包括已退場的）就把它交出來，最後交出的記錄存在時，頭像改用該記錄的 `+7`（頭像編號，轉職後會變）。接著：

- 找到未退場的單位：從它的格子 (`+0`, `+1`) 以縮放動畫開窗；
- 只有已退場的相符記錄：頭像取最後一個相符記錄的，以滑入動畫開窗；
- 地圖上完全沒有這個角色：頭像編號 = 角色編號，以滑入動畫開窗。

`fdps_battle_find_unit_by_character_id`（`0x2dc20`）裡「地圖上找不到就改查名冊」的那段因為條件寫錯而永遠不執行，所以第三種情況不會拿到名冊記錄的頭像。地圖記錄部署的單位由 `fdps_deploy_unit`（`0x232b0`）把頭像設成角色編號本身，所以對它們而言「頭像 = 角色編號」與記錄上的值相同；看得出差別的只有名冊成員——轉職會改寫他們的頭像——在自己不在目前單位陣列裡時說話。隊伍的 slot 由 `fdps_build_map_unit_array`（`0x22be0`）依名冊順序取前「地圖的我方 slot 數」名，超出的成員不在陣列裡，過場腳本切換到的場景地圖也以那張地圖的 slot 數重建陣列。

**-0x12（單位索引）**：以 `fdps_get_unit_record`（`0x2d210`）直接取記錄，不檢查索引也不看是否退場，頭像取 `+7`，一律從它的格子以縮放動畫開窗。

## 開窗

### 從說話者的格子縮放

`fdps_message_window_open_from_tile`（`0x20a70`），參數 `(tile_x, tile_y, face)`，在 `tile_x == -1` 時直接以 `face` 呼叫 `fdps_message_window_open`（`0x205b0`，見下）。否則：

1. 把地圖游標繪製模式 `data_fdps_map_cursor_draw_mode` 設 0，以 `(tile_x * 24, tile_y * 24)` 呼叫 `fdps_map_cursor_move_to`（`0x2d7c0`）把視野捲到說話者；捲完把模式**設成 1**（普通的方框游標），不還原進入時的值。
2. 說話者在畫面上的位置取捲動**之後**的視野原點：`sx = tile_x * 24 − data_fdps_battle_view_window_origin_x`，`sy` 同理。
3. 7 幀（`step` = 0..6），每幀重新合成場景（含游標），再以縮放模式 4 畫 `Message.cel` sprite 0：

   ```
   w = step * 302 / 6      （w == 0 時改成 4）
   h = step * 73 / 6       （h == 0 時改成 2）
   x = sx + 20 + (9   − sx) * step / 6
   y = sy + 20 + (120 − sy) * step / 6
   ```

   四個除法都是帶號、向 0 截斷；(x, y) 是合成頁座標。第 6 幀剛好是 302×73、畫面 (9, 120)，與滑入的結果同位置。每幀只呈現視野區，四周 4 像素的邊保留呼叫前的內容。
4. 幀與幀之間以 tick 節拍；節拍用的「上一個 tick」變數在第一幀之前沒有初始化，第一幀通常不等待（這類迴圈的共通陷阱見 [`rebuild_info/pitfalls.md`](../rebuild_info/pitfalls.md)）。

兩條路最後都呼叫 `fdps_load_and_draw_portrait`（`0x177d0`）把頭像畫在 (12, 90)。走滑入那條路時頭像因此被畫兩次，第二次疊在第一次上，看不出差別。

### 滑入

`fdps_message_window_open`（`0x205b0`），參數 `(face)`：

1. 把整個畫面（64000 byte）存成備份，把 `Message.cel` sprite 0 解到一塊先清成 0 的 302×73 緩衝（透明像素因此是色號 0）。
2. 6 幀，第 `i` 幀（0..5）：從備份重建整頁，把框以權重 `(2i + 5) / 16`（5/16、7/16 … 15/16）半透明混在 `row[i]` 列、第 9 行，`row` = {190, 170, 150, 135, 127, 120}；混合的高度是 `min(200 − row[i], 73)`，只裁畫面下緣。混合由 `fdps_blit_blend_rect`（`0x30230`）做，不遮罩色號 0，所以框的圓角在滑動中是暗色。整頁在兩段式歸線等待後呈現。
3. 最後一幀從備份重建，再把框以不透明的模式 0 畫在 (9, 120)，整頁呈現。每一幀都從同一份備份組成，不累積。
4. `face < 0`：釋放頭像緩衝並清成 NULL，不畫頭像；否則呼叫 `fdps_load_and_draw_portrait`（`0x177d0`）把頭像畫在 (12, 90)。

呼叫端在開窗後把文字畫在 `0xaa44a`，也就是框內頭像右邊。

## 頭像

`fdps_load_and_draw_portrait`（`0x177d0`），參數 `(dest, pitch, index)`：

1. 不論 `index` 為何，先 free 舊的頭像緩衝 `data_fdps_portrait_sprite_buf_ptr` 並設 NULL；`index == -1` 就到此為止（只有 -1，其他負值會照常往下走）。
2. `fopen("FACE.CEL", "rb")`，開不起來就印 `File not found: 'FACE.CEL'` 並 `exit(1)`。
3. seek 到 `index * 4 + 0x0f`，一次讀 8 byte 取得這一筆與下一筆的檔案位移，長度是兩者之差；malloc 這個長度、讀進來、關檔。只檢查 `fopen`，malloc、fread、fseek 的結果都不看。
4. 以模式 0 把 125×100 的頭像畫到 `dest`。

緩衝留著不釋放：等待按鍵、換頁與二選一提示在每一幀都從它重畫頭像到 (12, 90)，收窗也不釋放它，直到下一次開窗或畫頭像時才換掉。狀態面板 `fdps_draw_unit_status_panel`（`0x16300`）也用同一支 function 畫頭像，所以開過狀態面板之後，緩衝裡是那張頭像。`FACE.CEL` 的筆數與目錄見 [`resource_info/cel.md`](../resource_info/cel.md)。

## 等待按鍵

`fdps_message_window_wait_key`（`0x203d0`），參數 `(show_indicator, max_passes)`：

1. 進入時從**畫面上**把訊息框的矩形——(9, 120) 起 302×73——複製出來，把這份複本的四個角像素設成 0。這份複本包含呼叫前已經畫好的字。
2. 每一輪**先讀鍵**：`fdps_read_keyboard_queue`（`0x567be`）回傳的 byte 零延伸後 `<= 0x7f`（任何按下碼）就立刻結束，這一輪什麼都不畫。空佇列（`0xff`）與放開碼（bit 7 為 1）都不算。按鍵本身不保留，呼叫端拿不到是哪個鍵。
3. 沒有按鍵就畫一幀：背景（戰場重新合成場景、村莊複製畫面），疊上框的複本（跳過色號 0，所以四個角透出背景），`show_indicator` 非 0 時畫等待指示 `Command.cel` sprite `0x48 + ((int) last_tick / 3 & 3)`，頭像緩衝存在時畫頭像；兩段式歸線等待後呈現視野區。
4. 等 tick 計數器與 `last_tick` 不同，`last_tick` 更新為目前的 tick。`last_tick` 從 0 起，所以第一輪不等待，等待指示第一輪是 `0x48`，之後每 3 tick 換一格、4 格一循環。
5. `max_passes` 減 1，不為 0 就回到第 2 步。

所以等待**有時限**：`max_passes` 輪之後就算沒按鍵也結束。第一輪不等 tick，之後每輪至少一個 tick，所以 `n` 輪至少是 `n − 1` tick。文字直譯器的每一次等待都是 100 輪，約 99 tick、4 秒，沒人按鍵時對話每頁約 4 秒自動前進。死亡腳本（`fdps_run_death_scripts`（`0x1d990`））的「撿到物品」與「背包已滿」是 30 輪，「撿到金錢」與「物品遺失」是 50 輪。

等待期間戰場的場景每幀重新合成，但不呼叫 `fdps_cycle_scene_palette`（`0x2eab0`），所以場景的調色盤循環在等待按鍵時停住；二選一提示每輪都呼叫它，會繼續循環。

## 收窗

`fdps_message_window_close`（`0x20820`）不讀畫面、不還原開窗前的內容：

1. 以 `fdps_draw_scene_layers`（`0x2bf60`）重新合成場景，把視野區放進一張先清成 0 的整頁，得到「乾淨頁」：場景加上四周 4 像素的黑邊。
2. 把 `Message.cel` sprite 0 解成一張沒有字、沒有頭像的空框。
3. 6 幀，`i` 從 5 往下數到 0：從乾淨頁重建，把空框以權重 `(2i + 5) / 16` 混在 `row[i]` 列（`row` 同滑入的表），高度 `min(200 − row[i], 73)`。所以框從 120 列以 15/16 開始，一路滑到 190 列、淡到 5/16。
4. 最後把乾淨頁本身呈現上去，畫面上不留任何框。

所以從收窗的第一幀起，字、頭像與呼叫前畫面上的其他東西（例如狀態面板）都不見了。收窗不看 `data_fdps_village_mode_flag`，一律以 `fdps_draw_scene_layers`（`0x2bf60`）合成的地圖場景當背景。頭像緩衝不在這裡釋放。

## 二選一提示

`fdps_prompt_two_choice`（`0x17990`）沒有參數，回傳 0（左格）、1（右格）或 -1（取消）：

1. 進入時先清空鍵盤佇列，讓開啟提示的那個鍵不會直接作答；選擇從左格（0）開始。
2. 從畫面上複製 (9, 120) 起 302×73 的框（含呼叫端已畫好的問題），四個角設 0。
3. 迴圈（先測結束旗標再執行，旗標初值 0，所以至少一輪）：
   - 呼叫 `fdps_cycle_scene_palette`（`0x2eab0`）推進調色盤循環；
   - 背景與框、頭像的畫法同等待按鍵；
   - 沒被選的一格畫靜止圖（左格 sprite 8、右格 sprite 13），被選的一格**後畫**、播四格動畫：左格 `4 + ((int) last_tick / 3) % 4`、右格 `9 + ((int) last_tick / 3) % 4`；兩格都在合成頁第 `0x7b` 列，左格 x = `0xf9`、右格 x = `0x113`；
   - 兩段式歸線等待後呈現視野區，等下一個 tick；
   - **最後才讀鍵**（與等待按鍵相反，每一輪都先完整畫出一幀）。零延伸後 `< 0x7f` 才處理：Enter（`0x1c`）或空白（`0x39`）確定、保留目前的選擇；Esc（`0x01`）或小鍵盤 Del（`0x53`）確定並把答案設成 -1；←（`0x4b`）選左格、→（`0x4d`）選右格。其餘鍵忽略。
4. 沒有時限，不確定就不會返回。

取消一律回 -1，就算之前已經移到右格也一樣。所有呼叫端都只拿答案與 0 比較，所以左格是「是」，右格與取消都是「否」。提示不開窗也不收窗：戰場上的呼叫端先 `fdps_message_window_open`（`0x205b0`）、把問題畫進框內，提示結束後自己收窗；村莊裡則直接在村莊的視窗上發問。

## 數字欄位

`fdps_draw_number`（`0x17530`），參數 `(dest, pitch, value, digits, show_plus)`，不屬於對話文字，是狀態視窗、商店、存檔面板等處畫數字的共用元件，字形來自 `Number.cel`（6×8，每色 13 格）：

1. `digits == 0`：`sprintf("%d", value)`。
2. `digits > 0`：`limit = 10^digits`；`value >= limit`（帶號比較）時整欄畫成 `digits` 個 `?`，否則以 `"%.<digits>d"` 補零。負值永遠不會觸發 `?`，位數不夠時照樣畫出全部字元。
3. `show_plus` 的低 byte 非 0 且 `value >= 0` 時，在前面加 `+`。
4. 每個字元畫在 `dest + i * 6`：`'0'`..`'9'` → 格 0..9、`'+'` → 10、`'-'` → 11、`'?'` → 12，其餘 → 0；實際的 sprite 是 `data_fdps_number_glyph_color_row * 13 + 格`，顏色列由呼叫端事先寫入全域。

`digits` 為 0 與 `"%.0d"` 不等價：後者把 0 印成空字串，原版因此另走 `"%d"` 分支。

## 誰觸發對話

呼叫端分三種寫法：

**條目自己帶說話者碼**。章節事件處理函式、過場腳本、死亡腳本以 `dest` = `0xa0000`（畫面左上角）呼叫 `fdps_draw_text`（`0x1ff60`），畫本章文字的一筆；這些條目以說話者碼開頭，所以開窗、換人、等待與收窗全由條目決定，傳入的 `dest` 在第一個字之前就被說話者碼改寫掉。條目不以說話者碼開頭的話，字會直接畫在畫面左上角。這類呼叫者：

- 章節的 init、end、post-action 與事件處理函式，何時觸發見 [`chapter.md`](chapter.md)；
- 過場腳本直譯器 `fdps_icon_script_run`（`0x21650`）的畫文字 opcode，以及三選一 `fdps_icon_script_prompt_three_way_choice`（`0x22600`），見 [`cutscene.md`](cutscene.md)；
- 死亡腳本 `fdps_run_death_scripts`（`0x1d990`）的 opcode 3 以上，opcode 的意義見 [`resource_info/map.md`](../resource_info/map.md)。

**呼叫端自己開窗**。呼叫端以 `fdps_message_window_open`（`0x205b0`）開窗、傳入頭像編號，把一筆條目畫在 `0xaa44a`，再視需要呼叫 `fdps_prompt_two_choice`（`0x17990`），最後 `fdps_message_window_close`（`0x20820`）。等待按鍵來自條目結尾的 -3，或呼叫端自己呼叫 `fdps_message_window_wait_key`（`0x203d0`）——後者只有死亡腳本這樣做。這類呼叫者：死亡腳本的物品與金錢（`fdps_run_death_scripts`（`0x1d990`））、搜尋寶箱與埋藏（`fdps_battle_search_cell_at_cursor`（`0x184f0`））、道具效果（`fdps_apply_item_effect_to_targets`（`0x262a0`））、戰鬥系統選單的存讀檔與離開確認（`fdps_battle_system_menu`（`0x14ab0`）、`fdps_battle_system_submenu`（`0x14ea0`））、讀檔（`fdps_load_savegame`（`0x23e20`））、換片提示（`fdps_cd_verify_disc_and_play_track`（`0x30cc0`）），以及第 15、16、19、24、25 章的幾個事件（例如 `fdps_chapter_16_event_wandering_smith_forge`（`0x37cd0`））。

**村莊裡不開窗**。村莊的各個畫面自己把 `ShopWin.Cel` 的視窗畫好，把全域文字或本章文字畫在 `0xaa3d4`，需要回答時直接呼叫 `fdps_prompt_two_choice`（`0x17990`）；例如商店的買賣確認 `fdps_shop_buy_loop`（`0x33b80`）、教會轉職 `fdps_church_promote_loop`（`0x345a0`）、酒館 `fdps_run_bar_shop`（`0x35cc0`）。村莊的流程見 [`village.md`](village.md)。

升級訊息（`fdps_unit_award_exp_and_level_up`（`0x1dd30`））、勝敗條件視窗（`fdps_battle_show_win_fail_window`（`0x17ca0`））與各種清單只借用 `fdps_draw_text`（`0x1ff60`）在自己的面板上畫字，不經過訊息視窗。
