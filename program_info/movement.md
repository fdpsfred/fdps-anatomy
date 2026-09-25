# 移動範圍與地形

**驗證對象**：`FDPS.LE` 的移動網格與尋路 `0x10b20`–`0x119ac`、`0x119e0`–`0x11e4a`（`src/movegrid.c` 的九支：`fdps_map_grid_reset`、`fdps_move_grid_mark_zone_of_control`、`fdps_move_grid_set_stop_flag`、`fdps_move_grid_mark_opposing_zones_of_control`、`fdps_move_grid_flood_fill_range`、`fdps_move_path_trace`、`fdps_move_grid_block_occupied_tiles`、`fdps_battle_move_unit_toward`、`fdps_map_grid_collect_marked_tiles`）；沿路徑行走 `0x2d2d0`–`0x2d7be`（`src/walk.c` 全部五支）；一格的讀取 `0x2ba00`（`fdps_map_load_tile_info`，`src/maptile.c`）；地圖游標 `0x2b4f0`、`0x2c6a0`、`0x2d7c0`、`0x2da50`、`0x2dcf0`（`src/mapcur.c` 全部五支）。這些位址上「移動範圍怎麼算、路徑怎麼選、單位怎麼走、游標怎麼動」的結論以本檔為唯一正典。同樣住在 `src/maptile.c` 的格子事件回報與重繪（`0x2e030`、`0x2e910`）的語意屬於 [`resource_info/map.md`](../resource_info/map.md)；地圖圖層的檔案格式屬於 [`resource_info/terrain.md`](../resource_info/terrain.md)。

## 移動網格

移動範圍、目標範圍與尋路全部寫在同一塊堆積區塊上，指標是 `data_fdps_battle_move_grid_ptr`（`0x60144`）。`fdps_field_load_chapter_resources`（`0x227e0`）每次載入地圖時以第 0 層 `MPL` 的寬高配置 `4 + 寬 × 高 × 2` byte，寫進兩個帶號 16-bit 的寬高，再呼叫一次 reset。

| 位置 | 內容 |
| --- | --- |
| `+0` | `i16` 寬（格） |
| `+2` | `i16` 高 |
| `+4 + 2 × (y × 寬 + x)` | 第 (x, y) 格的 byte 0：旗標 |
| `+5 + 2 × (y × 寬 + x)` | 第 (x, y) 格的 byte 1：標記（marker） |

- 旗標 `0x40`：有一個對立單位站在這格，**不能進入**。
- 旗標 `0x80`：這格緊鄰一個對立單位，**走進來就得停**。
- 旗標低 6 bit：沒有任何程式讀寫，reset 保留它們（配置後是 `malloc` 回來的原值）。
- 標記：移動範圍擴散寫進去的累計消耗；`0xff` 是「沒有到達」的哨兵。目標範圍與游標模式 6 把它寫成 0 表示「被選到」。

`fdps_map_grid_reset`（`0x10b20`）對每一格做 `flags &= 0x3f`、`marker = 0xff`。它在網格未配置時直接返回；網格上的其他 function 都不檢查指標。寬高都以 `MOVSX` 讀、迴圈上界是帶號比較。

呼叫慣例是「用前網格已經 reset、用完自己 reset」：每一支建範圍的 function 都假設進來時網格是乾淨的，自己不先清。

## 一格的地形怎麼讀

`fdps_map_load_tile_info`（`0x2ba00`）是所有「這格是什麼」的共同入口，沒有回傳值，結果寫進一組全域：

| 全域 | 取自 |
| --- | --- |
| `data_fdps_map_tile_info_tile_id`（`0x69d04`） | 第 0 層 `MPL` 在 `+0x0b + 2 × (y × W + x)` 的 `i16` 圖磚編號，`W` 是 `MPL` 自己 `+7` 的寬 |
| `data_fdps_map_current_tile_attr_flags`（`0x69d08`） | 該圖磚 `ATTR` 列的 `+0` 旗標 |
| `data_fdps_map_tile_terrain_type`（`0x69d09`） | 該列的 `+2` 地形類別 |
| `data_fdps_map_current_tile_attr_reserved`（`0x69d0a`） | 該列的 `+1`，寫入後沒有人讀 |
| `data_fdps_map_tile_combat_backdrop_id`（`0x69d0b`） | 該列的 `+3` 戰鬥背景 |
| `data_fdps_map_current_move_grid_marker`（`0x69d0c`） | 移動網格第 (x, y) 格的標記 byte，**以 `MPL` 的寬索引**，不讀網格自己的寬 |
| `data_fdps_map_current_cell_event_code`（`0x69d06`） | 事件碼層 `DTL` 在 `+0x10 + y × Wd + x` 的 byte（零延伸），`Wd` 是 `DTL` 自己的寬 |

圖磚編號以 `MOVSX` 讀，乘 4 之後直接當屬性表的索引；沒有任何邊界檢查。各欄位的語意與兩種寬度不同的後果見 [`resource_info/terrain.md`](../resource_info/terrain.md)。移動判定只用其中兩個：地形類別（查移動消耗）與標記 byte（問「這格在不在範圍裡」）。

## 移動消耗：職業 × 地形

進入一格要付的點數是

```
cost = class_record->move_cost[terrain_type]      /* 兩者都是 u8，零延伸 */
class_record = fdps_get_class_record(unit->clazz + 1)
```

`fdps_get_class_record`（`0x18b70`）回傳 `PROMAP.DAT` 第 `index` 筆，第 0 筆是八個地形一律 1 的預設列，所以有單位在手的呼叫端都傳「職業代碼 + 1」（[`assets/tables/classes.md`](../assets/tables/classes.md)）。唯一漏了 `+1` 的呼叫端是 AI 的 `fdps_map_actor_move_toward_nearest_reachable_opponent`（`0x126b0`），見 [`map_ai.md`](map_ai.md) 與 [`rebuild_info/pitfalls.md`](../rebuild_info/pitfalls.md)。

- **付的是目的格的地形**：走進哪一格就查那一格的類別；出發格的地形不收費。
- `FF` 是「不可通行」。它不靠特別的判斷，而是靠算術：移動力是 u8，所以 `current + 255 > move_points` 只在 `current = 0` 且 `move_points = 255` 時不成立，而那時候選值 255 又不小於哨兵 255，一樣進不去。所以 `FF` 地形對任何 u8 的移動力都是牆。
- `move_cost[]` 只有 8 格而地形類別不設上限，類別 ≥ 8 會讀到同一筆的暴擊率與魔抗補數。出貨地圖只用到 0–6，走不到。
- 各職業的八個消耗值見 [`assets/classes.md`](../assets/classes.md)。從表上可以直接讀出幾條玩家看得到的規則：沒有任何職業的消耗是 0，所以每走一格至少花 1 點；類別 6 對全部職業都是 `FF`；類別 5 只有技師、機械伯爵、機械大師、飛兵、惡靈的消耗是 1，其餘職業過不去（攻略站把這一類叫「無法行走但可飛行的區域」，與表相符）；魔神（`1A`）與 `24`、`27` 八格全是 `FF`，站在原地永遠動不了。
- 移動力是單位記錄 `+0x3b` 的 u8（`unit->move`），出場值見 [`assets/characters.md`](../assets/characters.md) 與 [`assets/enemies.md`](../assets/enemies.md)。把它當移動力用或拿來顯示的只有四處（道具與轉職的加成只做加法寫回）：玩家移動 `fdps_battle_unit_turn`（`0x15470`）、朝目標移動 `fdps_battle_move_unit_toward`（`0x119e0`）、AI 攻擊評分 `fdps_map_actor_score_best_attack`（`0x12230`）與狀態面板 `fdps_draw_unit_status_panel`（`0x16300`），沒有任何一處以 25 為界。攻略站「MV 最好 25 以內」在程式裡沒有對應；移動力變大時真正會碰到的界線是：
  - **100**：路徑步數不超過移動力（見「從成本推回路徑」），而 `fdps_move_path_trace`（`0x11460`）暫存方向碼的區域陣列只有 100 byte、不檢查，所以移動力超過 100 且選了一條超過 100 步的路徑才會蓋到堆疊。其他容量都不看移動力：AI 攻擊評分的格清單有 `0x1900` byte（3,200 格），大於最大地圖的 825 格；擴散的佇列上限（見「範圍擴散」）由地圖大小決定。
  - **100**：狀態面板以 2 位數欄寬呼叫 `fdps_draw_number`（`0x17530`），值 ≥ 10² 時整欄畫成兩個 `?`，只影響顯示。
  - **255**：移動力是 u8，標記 byte 也是 u8，擴散在 255 以內都照上面的算術成立。

## 範圍擴散

`fdps_move_grid_flood_fill_range`（`0x10de0`）從起點向外一波一波地放寬，把每格的最小累計消耗寫進標記 byte：

```
marker(start) = 0
對每一波裡的每一格 c，依 上(y-1)、右(x+1)、下(y+1)、左(x-1) 的順序看存在的鄰格 n：
    candidate = marker(c) + move_cost[terrain(n)]           /* int 加法，兩邊都零延伸 */
    if (candidate <= move_points                            /* 帶號比較，可以剛好花完 */
        && (flags(n) & 0x40) == 0
        && candidate < marker(n)) {                          /* 嚴格小於 */
        if (flags(n) & 0x80) candidate = move_points;
        marker(n) = (u8) candidate;
        把 n 排進下一波
    }
```

- 鄰格的存在條件是 `y != 0`、`x < 寬 - 1`、`y < 高 - 1`、`x != 0`（帶號）；**只有四方向**，沒有斜走。
- 起點只寫一個 0，不看它的旗標：單位從緊鄰敵人的格子出發不受限制。
- 標記哨兵 `0xff` 同時是「還沒到過」與「比任何候選值都貴」，所以不必另外記「是否拜訪過」。嚴格小於表示同成本的第二條路既不寫也不排隊；一格被更便宜的路再次到達時會被重寫、重新排隊。
- **旗標 `0x80` 的格照樣收、照樣排隊，但存進去的是移動力本身**。因為每格消耗至少 1，從它出發的任何候選值都超過移動力，範圍在這一格截止；而這格本身之後仍可被更便宜的路重寫（寫回去的還是移動力）。結果就是「能用真實消耗走到的 `0x80` 格可以停，但不能穿過去」。
- 地形類別取自呼叫 `fdps_map_load_tile_info` 之後的 `data_fdps_map_tile_terrain_type`；每個方向在呼叫前另有讀了不用的載入（地形層的圖磚 byte、屬性表的地形類別），不改變任何結果。
- 兩波的佇列是 `data_fdps_battle_move_frontier_x`／`_y`（`0x63c50`／`0x63930`），各 800 byte、每波 400 格，索引一律是 `波 × 400 + 格`，沒有上限檢查；座標是 u8。出貨地圖最大 33 × 25，一波到不了 400。
- 呼叫端傳的移動力：單位自己的 `move`，或 `100`（`fdps_battle_move_unit_toward` 的放寬重試與 AI 的最近敵人搜尋，等於「整張圖」）。

同一支 function 也被拿來畫目標範圍：`fdps_collect_targets_in_range`（`0x11e50`）以第 0 筆預設職業列（每格消耗 1）擴散，射程於是以「可走的格數」計，會被站著單位的 `0x40` 格擋住。那一套規則見 [`battle.md`](battle.md) 與 [`spell.md`](spell.md)。

## 控制區與有人站的格

擴散之前先標控制區，擴散之後再把有人站的格拿掉，三支的順序固定是「標記 → 擴散 → 挖掉」。

`fdps_move_grid_mark_zone_of_control`（`0x10c30`）對一個單位 `(x, y)`：

- 四個鄰格各 `OR 0x80`，條件是 `x != 0`（左）、`y != 0`（上）、`寬 - 1 > x`（右）、`高 - 1 > y`（下）；
- 單位自己的格 `OR 0x40`。

兩個位元都是 OR，多個單位的控制區會疊在同一張網格上，只有 reset 會清掉。`fdps_move_grid_set_stop_flag`（`0x10da0`）是單格 `OR 0x80` 的版本，映像裡沒有任何呼叫者。

`fdps_move_grid_mark_opposing_zones_of_control`（`0x10b90`）與 `fdps_move_grid_block_occupied_tiles`（`0x118f0`）都拿同一個 `side_select` 參數，而且**只看真假值、正負相反**。單位記錄 `+6` 的陣營是 0 敵方、1 NPC、2 我方；退場的單位（`+5` 的 bit 0）兩支都跳過，本回合已行動（bit 7）的單位照樣算：

| `side_select` | 誰的控制區被標上 | 誰站的格被挖掉（`marker = 0xff`） |
| --- | --- | --- |
| 0（敵方在走） | 陣營不是 0 的單位：我方與 NPC | 陣營是 0 的單位：其他敵人 |
| 非 0（我方或 NPC 在走） | 陣營是 0 的單位：敵人 | 陣營不是 0 的單位：我方與 NPC |

`fdps_move_grid_block_occupied_tiles` 跳過 `exclude_unit_index`（正在走的單位），所以原地不動永遠合法。結果就是遊戲的通行規則：

- 對立陣營的單位站的格**進不去**，緊鄰它們的格**走進去就停**。
- 同一陣營（我方與 NPC 是同一邊）的單位站的格**可以穿過、不能停**：挖掉發生在擴散之後，穿過去的路徑已經算好了。
- NPC 擋我方的方式與我方隊友完全一樣；NPC 從不擋敵人，敵人的控制區也照樣作用在 NPC 身上。

`fdps_battle_unit_turn`（`0x15470`）與全軍前進傳 1；AI 傳它自己那一相的值（敵方相 0、NPC 相 1），見 [`map_ai.md`](map_ai.md)。部署找空格時以 0 與 1 各標一次，整張圖的控制區都會在網格上。

## 從成本推回路徑

`fdps_move_path_trace`（`0x11460`）的 `mode` 以一個 byte 讀，呼叫端只傳 0、1、2。

**模式 0 與 1**：從 `(start_x, start_y)` 一步一步走到 `(goal_x, goal_y)`，每一步：

```
threshold = marker(起點格)          /* 每一步都從「起點」的成本重設，不是目前這格 */
dir = 4
依 上、右、下、左 看存在的鄰格：
    模式 0：neighbour < threshold          → threshold = neighbour, dir = 該方向
    模式 1：上 同模式 0；右、下、左另外接受 neighbour == threshold，
            條件是 本步已選過方向（dir != 4）、已經記錄過至少一步、
            而上一個記錄的方向碼不是這個方向
    （最後一個方向「左」接受時不更新 threshold，因為後面沒有人讀）
依 dir 走一格：0 = y-1、1 = x+1、2 = y+1、3 = x-1、4 = 不動
走到 goal 才結束；dir != 4 才記一步
```

- 起點的標記是 `0xff` 時立刻回 -1，不寫輸出。否則回傳步數，方向碼以**反序**寫進 `out_path`（最後一步在最前面）。
- 所有呼叫端都把擴散的原點（單位自己的格，成本 0）當 `goal`、把目的地當 `start`：路徑是從目的地沿成本下坡走回單位。因為每格消耗至少 1，範圍內任何一格成本最低的鄰格就是它的上一格，所以下坡一定走得回原點、路徑的總消耗就是擴散算出的成本；緊鄰敵人的 `0x80` 格存的是移動力，永遠不會被選成「更便宜的鄰格」，路徑不會穿過它。
- 同成本的分岔：模式 0 保留掃描順序先看到的方向（上 > 右 > 下 > 左，這是從目的地往回看的方向）；模式 1 在同成本時偏好「轉彎」，路徑呈鋸齒而不是先直後橫。
- 迴圈只在站上 `goal` 時結束，找不到更低的鄰格就原地打轉。方向碼先存在 100 byte 的區域陣列裡，沒有上限。

**模式 2**：不走路。`start_x` 當成陣營的真假值（0 選陣營不是 0 的單位、非 0 選陣營是 0 的單位），跳過退場的，在剩下的單位裡找站在標記最小的格的那一個（嚴格小於，同值取索引小的），把它的 x、y 寫進 `out_path[0..1]`、回傳該成本；標記 `0xff` 的格不算，沒有人符合就回 -1。用途見 [`map_ai.md`](map_ai.md)。

## 玩家單位的移動

`fdps_battle_unit_turn`（`0x15470`）的每一圈：

1. 以**游標所在的格**當起點（回合開始時游標在單位身上），`fdps_move_grid_mark_opposing_zones_of_control(1)`、以單位的 `move` 擴散、`fdps_move_grid_block_occupied_tiles(unit, 1)`。
2. `fdps_map_cursor_select_loop`（`0x2b4f0`）模式 4：玩家移動游標，確認鍵只在標記不是 `0xff` 的格上成立——也就是範圍內、沒有別人站著的格，加上自己腳下。取消則游標走回起點、這個單位本回合沒有花掉。
3. reset，重新標控制區與擴散（**這次不挖掉有人站的格**），`fdps_move_path_trace` 模式 0 從游標格走回起點，再 reset。游標隱藏後走回起點。
4. 步數 0：原地行動；-1：整圈重來；其他：播放路徑、開行動選單。

路徑緩衝區是 `malloc(move)` byte，沒有檢查；因為每步至少花 1 點，步數不會超過移動力。

移動過之後除了琴琴（`07`）與裘娜（`03`）以外，行動選單的法術欄反灰；移動之後在行動選單按取消（且章節沒有設「取消即結束」旗標）會把單位的座標改回起點、游標走回去、整圈重來——每圈開頭都把待處理事件重設為 `0xff`，所以走的途中踩到的格子事件在取消後不會發生。行動選單與事件派送的其餘規則見 [`battle.md`](battle.md)、[`resource_info/map.md`](../resource_info/map.md)。

## 朝目標移動

AI 的每種走位與系統選單的「全軍前進」都呼叫 `fdps_battle_move_unit_toward`（`0x119e0`）：把單位盡量移近 `(dest_x, dest_y)`，有播放行走就回 1，選中的格就是自己腳下則回 0。呼叫端都會先把游標移到單位身上。它把網格重建四次，四輪各有不同：

| 輪 | 先 reset | 標控制區 | 移動力 | 挖掉有人站的格 | 用來做什麼 |
| --- | --- | --- | --- | --- | --- |
| 1 | 否 | 是 | 自己的 `move` | 否 | 模式 0 問「目的地付得起嗎」 |
| 2 | 是 | **否** | `100` | 否 | 只在第 1 輪回 -1 時跑：模式 1 問「不管控制區與移動力，有沒有路」 |
| 2b | 是 | 是 | 自己的 `move` | 否 | 第 2 輪有路時：沿那條路改目的地 |
| 3 | 是 | 是 | 自己的 `move` | 是 | 列出能停的格，挑最近的 |
| 4 | 是 | 是 | 自己的 `move` | 否 | 模式 0 算實際要走的路 |

**改目的地**（第 2b 輪）：把第 2 輪的方向碼從單位的格開始逐一**反向**重播（0 → y+1、1 → x-1、2 → y-1、其他 → x+1），每到一格就用 `fdps_map_load_tile_info` 讀標記，標記不是 `0xff` 就記下這格；重播完，**最後一個**通過的格成為新的目的地。等於把「走不到的目標」沿著真正的路線拉回到這回合付得起的最遠處。

**挑格**（第 3 輪）：`fdps_map_grid_collect_marked_tiles` 列出的每一格 `(x, y)`：

```
d    = abs(x - dest_x) + abs(y - dest_y)
skew = abs(abs(x - dest_x) - abs(y - dest_y))
if (d < best_d || (d == best_d && skew < best_skew)) 選它
best_d、best_skew 起始 255
```

`dest` 是改過之後的目的地。距離相同時偏好落在朝目標的對角線上的格；兩者都相同時保留先列出的格，也就是**列優先順序裡最上、再最左**的那格。距離是直線（曼哈頓）距離，不是路徑長。原地那一格成本 0、永遠在清單裡，所以一定選得出一格。

**走**（第 4 輪）：從選中的格走回單位的格算路徑，步數不為 0 就交給 `fdps_animate_move_path` 播放。最後網格被 reset，這支 function 算過的東西都不留給呼叫端。第 4 輪不挖掉有人站的格，所以路徑可以穿過同陣營的單位。

緩衝區是 `malloc(100)` 的方向碼與 `malloc(0x800)` 的格清單（1,024 格，大於最大地圖的 825 格）。

「全軍前進」由 `fdps_battle_system_menu`（`0x14ab0`）執行：在確認的那一刻取游標格當共同目的地，依單位索引順序，對每個 `+5 & 0x85 == 0`、陣營 2、麻痺計數 `status_timers[4] == 0` 的單位以 `side_select = 1` 呼叫本 function，然後標記行動完畢。所有人都朝同一格走。

## 沿路徑行走

`fdps_animate_move_path`（`0x2d2d0`）逐 byte 讀方向碼，只比 0、1、2，其他值一律走右邊；步數以帶號比較，-1 不走：

| 碼 | 呼叫 | 方向 | 面向（單位 `+3`） |
| ---: | --- | --- | ---: |
| 0 | `fdps_walk_step_down`（`0x2d360`） | y + 1 | 0 |
| 1 | `fdps_animate_move_step_left`（`0x2d590`） | x − 1 | 1 |
| 2 | `fdps_animate_move_step_up`（`0x2d480`） | y − 1 | 2 |
| 其他 | `fdps_animate_move_step_right`（`0x2d6a0`） | x + 1 | 3 |

這張表剛好是 `fdps_move_path_trace` 方向碼的反向：路徑是從目的地往回記、再反序存放的，順著播放就得把每一步倒過來走。

每走一格：

1. 設面向，量一次**出發時**的像素座標（格 × 24），往下與往右的另外量地圖的像素高／寬（第 0 層 `MPL` 的 `+9`／`+7` × 24）。
2. 六個子步，`+4` 依序寫 1..6，游標世界座標朝行走方向每步移 4 像素（共 24）。每個子步檢查視窗是否跟著捲 4 像素，`o` 是視窗原點、`p` 是第 1 步量的出發像素：

   | 方向 | 條件 |
   | --- | --- |
   | 下 | `p_y - o_y > 120` 且 `o_y < 地圖高 - 192` |
   | 上 | `p_y - o_y < 48` 且 `o_y >= 4` |
   | 左 | `p_x - o_x < 48` 且 `o_x >= 4` |
   | 右 | `p_x - o_x > 240` 且 `o_x < 地圖寬 - 312` |

   `p` 不隨子步更新，所以往下／往右時差距每步縮 4，往上／往左時每步增 4，捲動會在中途自己停下。全部是帶號比較。
3. 最後一次按下的掃描碼是 2（主鍵盤 `1`）或 3（主鍵盤 `2`）時，子步不畫；六步後掃描碼是 2 就補畫一格。其他情況每個子步畫一格。
4. 單位座標 ±1、子步歸 0，以**游標座標 ÷ 24**（帶號除法）當到達的格，呼叫 `fdps_map_set_pending_tile_event(…, 0)`（`0x2e030`）回報「移動中走進這格」。用游標而不是單位記錄，兩者只在游標跟著單位時一致，而每個呼叫端都先把游標放到單位身上。事件的比對與派送見 [`resource_info/map.md`](../resource_info/map.md)。

## 地圖游標

游標是世界像素座標 `data_fdps_map_cursor_world_x`／`_y`，視窗原點是 `data_fdps_battle_view_window_origin_x`／`_y`，視窗 312 × 192 像素。

**游標移動**：`fdps_map_cursor_move_to`（`0x2d7c0`）把游標一步步移到目標像素，`fdps_map_cursor_move_to_unit`（`0x2da50`）以單位的 `(x × 24, y × 24)` 呼叫它（不檢查單位索引）。

```
dx = tx - cx; dy = ty - cy;   都是 0 就返回
if (abs(dx) > abs(dy)) { n = abs(dx) / 24; ax = dx / n; acc = dy;
    每步: cx += ax; cy += acc / n; acc = dy + acc % n; }
else                   { n = abs(dy) / 24; ay = dy / n; acc = dx;
    每步: cy += ay; cx += acc / n; acc = dx + acc % n; }
```

除法全是向零截斷的帶號除法。主軸移 `n × (主軸 / n)`，只在整除時到位；次軸靠 `acc` 把餘數分攤到各步，總和剛好是次軸的差。兩軸差都不到 24 時 `n = 0` 會除以零——所有呼叫端的目標都對齊格子，走不到。每步之後依序做四個獨立的視窗調整：`cy - oy > 144` 則 `oy = cy - 144`，再若 `oy + 192 > 地圖高` 則 `oy = 地圖高 - 192`；`cy - oy < 24` 則 `oy = cy - 24`，再若 `oy < 0` 則 0；x 軸同樣，遠端界線是 216、視窗寬 312。地圖寬高取自移動網格的表頭 × 24。游標可見（繪製模式非 0）或視窗動了才畫一格；隱藏的游標在不捲動時瞬間到位。

**選格迴圈**：`fdps_map_cursor_select_loop`（`0x2b4f0`）每圈畫一格畫面，按鍵以最後一次的掃描碼判斷：

| 按鍵 | 作用 |
| --- | --- |
| 方向鍵 | 游標移 24 像素：上要 `cy >= 24`、下要 `地圖高 - 24 > cy`，左右同理。同一個掃描碼連續出現時第 1 圈移動、第 2–6 圈不動、第 7 圈起每圈移動；每次移動播 `Beep.wav` |
| `Esc`、`Del` | 取消，下一圈回 -1 |
| 空白、`Enter` | 確認，依模式判定 |
| `Z`、數字鍵盤 `5` | 有候選清單時跳到下一個候選單位 |

視窗一次推 24 像素：`o_x >= 24` 且 `cx - o_x < 24` 往左；`cx - o_x > 264` 且 `地圖寬 - 312 > o_x` 往右；y 軸同理，下方界線 144、視窗高 192。

確認的判定：模式 4（玩家選目的地）是游標格的標記不是 `0xff`；模式 6（傳送術選落點；道具效果 `0x19`、`0x1C` 也有同樣的呼叫，但出貨資料走不到。`list_count` 位置傳的是被移動的單位）是游標格上沒有未退場的單位、且被傳送單位的職業在該格地形的消耗 `< 0x14`（不查範圍，整張圖都可以）；模式 5 只能取消；其他模式交給目標判定，見 [`spell.md`](spell.md)。

**游標圖樣**：`fdps_draw_map_cursor`（`0x2c6a0`）在每次畫面合成時依繪製模式畫：1、2 是單格游標，3、4、5 是半徑 1、2、3 的菱形範圍（模式 5 不畫緊鄰中心的四格），模式 6 什麼都不畫，而是把游標所在格的標記寫成 0。在模式 6 下移動游標，走過的每一格都被「選到」，格數由畫了幾格畫面決定。這些模式由法術、道具與 AI 設定（[`spell.md`](spell.md)、[`map_ai.md`](map_ai.md)）。

**地形資訊面板**：`fdps_draw_cursor_info_panel`（`0x2dcf0`）在地形面板開啟且遊戲在玩家操作狀態時，畫出游標格的圖磚與兩個地形修正值（`+` 號、依地形類別查表，數值見 [`resource_info/terrain.md`](../resource_info/terrain.md)），格上有單位時再畫它的圖與 HP（未滿血用第 3 色列）。面板的欄位由游標在視窗內的格決定：列 > 4 且欄 < 2 時停到右側（x = 292），列 > 4 且欄 > 10 時停到左側（x = 25），其他情況留在原處；初值 25。

## 網格的其他讀法

`fdps_map_grid_collect_marked_tiles`（`0x11da0`）以列優先順序掃整張網格，標記不是 `0xff` 的格寫一組 `(x, y)` byte 進呼叫端的緩衝區、回傳組數。它不收容量參數也不檢查，緩衝區夠不夠大由呼叫端負責：移動用的是 1,024 組，AI 的道具與法術評分只給 200 組（[`map_ai.md`](map_ai.md)）。

範圍在畫面上的顯示不在本檔：屬性模式非 0 的圖層在標記不是 `0xff` 的格上隨脈動相位變色（`fdps_draw_scene_layer`（`0x2c330`），規則見 [`resource_info/terrain.md`](../resource_info/terrain.md)），`fdps_battle_unit_turn`（`0x15470`）在每個我方單位的回合開始時把相位設成 `0x14`。

## 相關文件

- 圖層格式、地形類別與攻防修正表：[`resource_info/terrain.md`](../resource_info/terrain.md)
- 格子事件的觸發時機與派送：[`resource_info/map.md`](../resource_info/map.md)
- 職業的地形消耗：[`assets/classes.md`](../assets/classes.md)、[`assets/tables/classes.md`](../assets/tables/classes.md)
- AI 怎麼決定往哪走：[`map_ai.md`](map_ai.md)
- 行動選單、目標範圍與攻擊：[`battle.md`](battle.md)；法術與傳送：[`spell.md`](spell.md)
- 重建時的陷阱：[`rebuild_info/pitfalls.md`](../rebuild_info/pitfalls.md)
