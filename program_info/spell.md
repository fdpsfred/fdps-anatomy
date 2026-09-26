# 法術與道具效果

**驗證對象**：`FDPS.LE` 的法術與道具子系統，本檔是下列 function 的唯一正典——`src/spell.c` 的 `0x28320`–`0x29201`（`fdps_spell_damage_unit`、`fdps_spell_heal_unit`、`fdps_spell_deduct_mp_cost`、`fdps_play_spell_11_cutscene`、`fdps_cast_spell_on_targets`、`fdps_play_spell_palette_flash`）；`src/spellmnu.c` 的 `0x276f0`–`0x282a4`（`fdps_draw_spell_list_page`、`fdps_spell_list_window_wait_input`、`fdps_battle_spell_command`、`fdps_spell_list_select_loop`）；`src/cmbspell.c` 的 `0x1a4c0`（`fdps_combat_play_spell_on_targets`）；`src/item.c` 的 `0x252b0`（`fdps_battle_item_menu`）、`0x26230`（`fdps_apply_damage_to_targets`）、`0x262a0`（`fdps_apply_item_effect_to_targets`）、`0x26fd0`（`fdps_apply_heal_to_targets`）；`src/unititem.c` 的 `0x25140`–`0x26173` 與 `0x34520`（物品欄與裝備）。另外本檔擁有下列在別的 `src/` 檔、但只為法術與道具服務的效果規則：`src/unitstat.c` 的 `0x27070`（`fdps_unit_apply_heal`）、`0x275a0`（`fdps_unit_restore_mp`）、`0x27840`（`fdps_unit_collect_known_spells`）、`0x28460`（`fdps_unit_apply_damage`）、`0x28ee0`（`fdps_unit_inflict_random_ailments`）、`0x28f70`（`fdps_unit_apply_status_effect`）、`0x29080`（`fdps_unit_is_ailment_immune`），與 `src/unit.c` 的 `0x282b0`（`fdps_set_flag_bit`）。`fdps_unit_recompute_combat_stats`（`0x24d70`）的戰鬥數值重算公式歸 [`battle.md`](battle.md)。

法術與物品的數值（威力、命中率、距離、範圍、MP、對象、物品的使用效果碼與數量）在 [`../assets/spells.md`](../assets/spells.md) 與 [`../assets/items.md`](../assets/items.md)，record 佈局在 [`../assets/tables/spells.md`](../assets/tables/spells.md) 與 [`../assets/tables/items.md`](../assets/tables/items.md)；本檔只寫程式怎麼用它們。本檔的欄位名沿用 `src/fdpstype.h`：法術 record 的 `power`（`+0x00`）、`hit_rate`（`+0x02`）、`cast_range_flags`（`+0x03`）、`area`（`+0x04`）、`mp_cost`（`+0x05`）、`target_side`（`+0x06`）；物品 record 的 `use_effect`（`+0x0d`）、`use_amount`（`+0x0e`）、`use_distance`（`+0x10`）、`use_target`（`+0x11`）、`use_radius`（`+0x12`）、`select_mode`（`+0x15`）。

## 施法的前提與法術選單

戰鬥行動選單的「法術」項在三種情況下反灰：

- 單位一個法術都不會——`fdps_battle_action_menu`（`0x15d00`）以 `fdps_unit_collect_known_spells` 的計數判斷；
- 單位身上的封魔咒術計時（單位記錄 `+0x27`，`status_timers[5]`）不為 0，同一支 function 判斷；
- 單位這回合移動過——`fdps_battle_unit_turn`（`0x15470`）在路徑步數非 0 時把法術項設成反灰，只有角色編號（單位記錄 `+0x08`）為 `07` 琴琴與 `03` 裘娜的單位豁免；原地不動則清回可選。

已學法術記在單位記錄 `+0x1a` 起的 5 byte 位元遮罩，bit 編號就是法術編號（佈局見 [`../assets/tables/spells.md`](../assets/tables/spells.md)）。`fdps_unit_collect_known_spells`（`0x27840`）依 byte 0→4、bit 0→7 的順序把設了的位元展開成一串遞增的法術編號，最多 40 個。學會新法術是 `fdps_set_flag_bit`（`0x282b0`）對同一張遮罩 OR 一個位元：升級時依習得表呼叫（習得表見 [`../assets/characters.md`](../assets/characters.md)），另外蓋亞的強化套件（`use_effect` `0x21`）直接給 `0x1d` 轟神砲，第 1 章的結束處理 `fdps_chapter_01_end`（`0x3a410`）在寫回名冊之前給戰場單位 0（蘭迪斯）`00` 業火（見 [`../rebuild_info/pitfalls.md`](../rebuild_info/pitfalls.md)）。

法術清單一頁 8 列，由 `fdps_draw_spell_list_page`（`0x276f0`）畫：名稱是全域文字 `0x1be + 法術編號`，右側的 MP 消耗是 `mp_cost` 無號讀出、4 位補零（消耗 12 顯示 `0012`）。清單以「清單索引」取編號，第二頁畫的是第 9 個以後的法術。

`fdps_spell_list_select_loop`（`0x28130`）處理按鍵：上下移動游標，游標走出頁面時頁面才跟著捲（往下捲時游標停在該頁最後一列）；確認鍵只有在 `mp_current`（`+0x44`，有號 16-bit）`>= mp_cost`（無號 byte）時才成立，MP 相等可以施放；MP 不足時確認鍵**沒有任何反應**，不發聲也不提示；Esc／Delete 取消。清單的捲動位置與游標列存在 `fdps_battle_spell_command`（`0x27c20`）的重試迴圈之外，所以瞄準階段取消後重開的清單停在同一頁、同一個法術上。

## 目標選取與範圍

`cast_range_flags` 同時帶距離與形狀：`0` 表示以施法者自己為中心、`0x01`–`0x0f` 是一般距離、`0x10` 以上是直線（長度為 `值 − 0x10`）。`fdps_battle_spell_command`（`0x27c20`）依這個值走三條路，三條路都先把游標的繪製模式設成 `area + 2`（游標上畫出半徑 `area` 的菱形）。

**一般距離（`0x01`–`0x0f`）**：

1. 可瞄準範圍由 `fdps_collect_targets_in_range`（`0x11e50`）從施法者所在格（游標此刻停在施法者身上）算出，`range_code = cast_range_flags`、`min_dist = 0`（施法者自己的格也算），選取模式 = `target_side`。`range_code < 0x10` 時是以 `PROMAP.DAT` 第 0 列（每種地形消耗 1）做洪水填充，包含端點；呼叫時網格已重設、沒有任何單位佔格旗標，所以範圍就是地圖內 `|Δx| + |Δy| <= cast_range_flags` 的菱形，地形與單位都擋不住（收集規則見 [`map_ai.md`](map_ai.md) 的「目標收集」）。
2. 玩家在 `fdps_map_cursor_select_loop`（`0x2b4f0`）移動游標；確認鍵要求游標格在上一步標出的範圍內，**而且**以游標為中心、曼哈頓距離 `<= area` 之內至少有一個選取模式接受的單位（這一步不看地形）。
3. 確認後的命中名單是**第二次** `fdps_collect_targets_in_range`：從游標格出發、`range_code = area`、`min_dist = 0`、選取模式 = `target_side`。這次同樣是在剛重設的網格上以第 0 列洪水填充，範圍就是地圖內以游標為中心、`|Δx| + |Δy| <= area` 的菱形，與確認時的菱形相同，隔著地形也照樣打到。

**直線（`0x10` 以上）**：可瞄準範圍同樣由 `fdps_collect_targets_in_range` 算，`range_code >= 0x10` 時是施法者所在列與所在行上、距離 `<= 值 − 0x10` 的十字形，不看地形。確認後的命中名單由 `fdps_collect_targets_in_line`（`0x13670`）從施法者的格往游標方向走 `cast_range_flags − 0x10` 格：游標與施法者的 x 不同就只沿水平方向走（y 差完全忽略），x 相同才沿垂直方向走；起點格不算；每格只收**陣營 0** 的單位，不看 `target_side`。走完播 `Chess.wav`。

**以自己為中心（`0`，只有 `0A` 裂地術與 `0B` 封神裂震）**：命中名單在瞄準之前就定了——從施法者的格、半徑 `area` 做洪水填充，固定只收陣營 0，不看 `target_side`；接著游標繪製模式改成一般游標，瞄準迴圈以模式 4（任何標出的格都能確認）執行，名單為空時改用模式 5（確認鍵永遠不成立，只能取消）。游標停在哪裡不影響打到誰。

選取模式（`target_side` 或物品的 `select_mode`）在命中名單收集時由 `fdps_collect_targets_in_range`（`0x11e50`）解讀，確認鍵那一步由另一支 `fdps_collect_targets_in_area`（`0x109f0`）解讀：兩者都是 0 收陣營 0（敵方）、1 收陣營不是 0（我方與 NPC）、3 收陣營 2（我方）、4 以上不收，只有 2 的意義不同。兩張模式表見 [`map_ai.md` 的「目標收集」](map_ai.md#目標收集)。出貨的法術只用 0、1、3。

**傳送術（`15`）** 在確認目標之後多一段：命中名單的第一個單位若就是施法者，整次施法作廢並回到清單；否則以 `fdps_map_cursor_select_loop` 的模式 6 選目的格——全圖任何一格都可以，條件只有該格上沒有在場單位、且**被傳送者**的職業對該格地形的消耗小於 20。目的座標存進 `data_fdps_battle_teleport_dest_tile_x`／`data_fdps_teleport_destination_tile_y`。

瞄準被取消時，兩次收集照樣會跑完，只是結果不被使用；游標回到施法者身上並重開法術清單。

## MP 消耗

`fdps_spell_deduct_mp_cost`（`0x285c0`）：

```
mp_current = (short)(mp_current - mp_cost)    // mp_current 有號 16-bit，mp_cost 無號 byte
```

沒有「付不付得起」的檢查，也沒有下限；付不起的檢查只在玩家的法術選單裡（上一節）。MP 每次施法只扣一次、與命中幾個目標無關，全部落空也照扣。扣的時機依演出路徑而不同：全螢幕演出在「蓄力」段播完之後才扣（`fdps_combat_play_spell_on_targets`（`0x1a4c0`）內），地圖演出則在最前面、閃光之後立刻扣（`fdps_cast_spell_on_targets`（`0x288f0`）內，同一段運算直接展開在它的本體裡）。

## 施法的兩種演出與分派

玩家施法時 `fdps_battle_spell_command`（`0x27c20`）**一律**呼叫 `fdps_combat_play_spell_on_targets`，不看「戰鬥動畫」開關；AI 施法由 `fdps_map_actor_cast_chosen_spell`（`0x13c90`）依開關選兩者之一（見 [`map_ai.md`](map_ai.md)）。

`fdps_combat_play_spell_on_targets`（`0x1a4c0`）遇到下列法術直接轉給 `fdps_cast_spell_on_targets` 在地圖上演出，其餘在全螢幕戰鬥畫面演出：`0A` 裂地術、`0B` 封神裂震、`0E`–`16`（三個回復、封魔咒術、腐毒術、麻痺術、神之祝福、傳送術、神行術）、`18` 甦癒術、`21` 鎮魂之歌。這些正是全螢幕畫面沒有效果處理的法術——全螢幕路徑**只會造成傷害**。

### 全螢幕演出

`fdps_combat_play_spell_on_targets`（`0x1a4c0`）載入施法者陣營決定的一組三段特效（陣營 0 用 `E` 開頭、其餘用 `M` 開頭的 `%sB%02d.saf`／`%sL%02d.saf`／`%sE%02d.saf`），部分法術再加一個 `%sS%02d.saf` 疊層；背景是**施法者**所在格的地形背景（編號先減 1）。畫面分五段：

1. 施法者的 `Magic` 動作播到它第 0 格記的標記格為止。
2. 蓄力特效（`B`）播完一遍，然後扣 MP。
3. 對每個目標依序：先以 `fdps_spell_damage_unit` 決定傷害並**立刻寫進**目標的 HP，再把施法前的 HP 寫回去，接著讓主特效（`L`）在「命中格」上逐步扣回——第 `k` 個命中格之後 `hp = hp_before − (hp_before − hp_after) × k / 命中格數`（整數除法向零截斷）。主特效裡一個命中格都沒有時印出 `ERROR: No Hit Point !!!` 並結束程式。命中時目標被往遠離施法者的方向擊退 6 格並依法術的色帶變色；`0D` 鬼動死靈陣命中後另加隨機異常（見「狀態效果」）。目標之間以 16 格的交替動畫換人。
4. 主特效播到結尾（不再迴圈）。
5. 收尾特效（`E`）播完。

全螢幕路徑沒有傷害數字與 MISS 字樣，傷害只以 HP 條表現。

### 地圖演出

`fdps_cast_spell_on_targets`（`0x288f0`）的順序：

1. 游標隱藏，`fdps_play_spell_palette_flash`（`0x29100`）把 DAC 第 0 號色以法術的招牌色與黑色交替閃 4 次（共 8 個畫面），結束時第 0 號色**留在黑色**、不還原。
2. 扣 MP。
3. 特效名是 `Emg%02d.saf`（法術編號）；`0B` 封神裂震用的是 `0B − 1`，即裂地術的 `Emg10.saf`（`MISC.VFS` 沒有 `EMG11.SAF`）。
4. `0B` 另播全螢幕過場 `fdps_play_spell_11_cutscene`（`0x28610`）並以白閃淡回地圖；`21` 鎮魂之歌另播 `Emg33-1.saf`。
5. `0A`／`0B` 播 `EarQu.wav` 並震動畫面 25 格，每格兩軸各偏移 `rand() % 4 − 2`（`−2`..`+1`，不對稱），共消耗 50 次 `rand()`。
6. 特效播在所有目標上，然後依法術做效果（下一節），最後統一播放浮動數字與字樣，游標恢復成一般游標。

## 效果分派

`fdps_cast_spell_on_targets`（`0x288f0`）依法術編號分派，逐一套用到命中名單的每個單位：

| 法術 | 效果 |
| --- | --- |
| `0E` 恢復之光、`0F` 治癒之風、`10` 痊癒之泉、`21` 鎮魂之歌 | `fdps_unit_apply_heal(目標, power)`，浮出回復數字 |
| `15` 傳送術 | 游標捲到目的格，把名單**第一個**單位的座標改成目的座標，然後在名單上再播一次特效 |
| `16` 神行術 | 清掉目標單位記錄 `+0x05` 的 bit 7（本回合已行動），讓它再動一次；沒有命中判定 |
| `18` 甦癒術 | 清掉三個異常計時（`+0x25`..`+0x27`）；只有原本有異常的目標浮出 CURE；沒有命中判定 |
| `11` 封魔咒術 | 目標先以色 `0x2b` 閃兩次，再各自 `fdps_unit_apply_status_effect(0x11, 目標)`，失敗浮出 MISS |
| `12` 腐毒術、`13` 麻痺術 | `fdps_unit_apply_status_effect(法術編號, 目標)`，失敗浮出 MISS |
| `14` 神之祝福 | 三輪（計時槽 0、1、2）依序各對全部目標呼叫 `fdps_unit_apply_status_effect(槽號, 目標)`，成功的目標浮出該 buff 的圖示並重算戰鬥數值；每輪結束先把浮動圖示播完再進下一輪。失敗不顯示 MISS |
| 其餘 | `fdps_spell_damage_unit`：回傳非 0 浮出傷害數字，`0D` 鬼動死靈陣再加 `fdps_unit_inflict_random_ailments`；回傳 0 浮出 MISS |

回復與傷害的數字都是**擲出的值**，不是 HP 實際的增減（見下兩節）。`fdps_spell_heal_unit`（`0x28570`）是「以法術威力回復一個單位」的封裝，映像裡沒有任何呼叫者。

## 傷害公式

`fdps_spell_damage_unit`（`0x28320`）先算名目傷害，再擲命中：

```
mrc = PROMAP[target.clazz + 1].magic_resist_complement      // 無號 byte，存的是 100 − 魔抗
if (power >= 0)
    nominal = power * mrc / 100
else
    nominal = caster.ap * (-power) / 100 - target.dp        // ap = +0x48、dp = +0x4a，有號 word
    if (nominal < 0) nominal = 0

if (rand() % 100 < hit_rate) {
    if ((spell_id == 0x0a || spell_id == 0x0b) && fdps_unit_is_flying(target))
        return 0
    return fdps_unit_apply_damage(target, nominal)
}
return 0
```

- 所有除法都是有號除法、向零截斷；`power`、`ap`、`dp` 有號，`hit_rate` 與 `mrc` 無號。
- 正威力的魔法傷害完全不看防禦力與施法者的攻擊力；負威力（絕招，`power` 存的是加乘率的負百分比）才用施法者的 `ap` 與目標的 `dp`。職業表的魔抗欄位意義見 [`../assets/tables/classes.md`](../assets/tables/classes.md)。
- 命中只擲 `rand() % 100 < hit_rate`，不看閃避、地形或等級；`hit_rate` 100 必中。
- 命中的 `rand()` 在飛行判定**之前**擲，所以對飛行單位施放裂地術／封神裂震也會消耗一次 `rand()`。飛行單位是 `fdps_unit_is_flying`（`0x12550`）寫死的五個職業代碼 `0x16`、`0x17`、`0x18`、`0x1f`、`0x25`。

`fdps_unit_apply_damage`（`0x28460`）把名目傷害擲成實際傷害並計入經驗：

```
rolled = nominal * 9 / 10 + (rand() % 100) * nominal / 1000
hp = (unsigned short)hp_current - rolled
if (hp < 0) hp = 0
hp_current = hp
if (target.side == 0) {
    exp = ENEMYDAT[target.portrait_id - 0x3c].exp_reward * target.level
    if (hp != 0)
        exp = exp * rolled / (unsigned short)hp_max
    pending_xp += exp
}
return rolled
```

擲出的範圍是名目值的 0.900–0.999 倍。回傳（也就是浮出的數字）是 `rolled`，過量傷害照樣顯示全額；`rolled` 為 0 時上層顯示 MISS，所以名目傷害 1 的攻擊一律顯示 MISS。把目標打到 0 的一擊拿到全額的 `exp_reward × level`，沒打倒時按 `rolled / hp_max` 的比例給。HP 兩個欄位在這裡是**無號**讀出。只看陣營 0 就去查 `ENEMYDAT.DAT` 的後果見 [`../rebuild_info/pitfalls.md`](../rebuild_info/pitfalls.md)。

道具的直接傷害走同一支 `fdps_unit_apply_damage`，但不經過命中與魔抗：`fdps_apply_damage_to_targets`（`0x26230`）與光束砲類（`use_effect` `0x1e`）把 `use_amount` 直接當名目傷害，必中、不看防禦。

## 回復公式

`fdps_unit_apply_heal`（`0x27070`）：

```
base  = amount * 9 / 10
bonus = (rand() % 100) * amount / 1000
hp = hp_current + base + bonus          // hp_current、hp_max 有號讀出
if (hp > hp_max) hp = hp_max
restored = hp - hp_current
hp_current = hp
if (target.portrait_id < 0x3c) {
    lv = target.level
    if (0x0f <= target.portrait_id && target.portrait_id <= 0x21) lv += 30
    pending_xp += lv * 25 * restored / hp_max
}
return base + bonus
```

浮出的數字是擲出的 `base + bonus`，不是實際補到的量；目標已滿血時照樣浮出全額而經驗為 0。經驗以**被補的單位**的等級計算，被補的是敵方肖像（`>= 0x3c`）時不計。沒有下限，`amount` 為負時會扣血。

`fdps_unit_restore_mp`（`0x275a0`）是 MP 版：同樣的 `base + bonus` 擲法，上限夾到 `mp_max`，回傳擲出值；不同處是兩個 MP 欄位以**無號**讀出（負的 `mp_current` 被當成接近 65535 而直接夾到上限），而且不計經驗。

`fdps_apply_heal_to_targets`（`0x26fd0`）是道具回復的包裝：不論目標幾個，先在名單上播 `Posion.saf`（原版就是這個拼法）再全體閃白，然後逐一 `fdps_unit_apply_heal` 並浮出數字。

## 狀態效果

單位記錄 `+0x22` 起的 6 個計時 byte：

| 槽 | 位移 | 效果 | 由誰設 |
| ---: | --- | --- | --- |
| 0 | `+0x22` | 攻擊力 ×1.15 | 神之祝福 |
| 1 | `+0x23` | 防禦力 ×1.15 | 神之祝福 |
| 2 | `+0x24` | DX +15（命中與閃避都加） | 神之祝福 |
| 3 | `+0x25` | 中毒 | 腐毒術、鬼動死靈陣、武器附加效果、第 1 章開場處理 `fdps_chapter_01_init`（`0x20e90`，對索爾設 11） |
| 4 | `+0x26` | 麻痺 | 麻痺術、鬼動死靈陣、武器附加效果、第 5 章開場過場 `ICON04.DAT` 的 `SET_UNIT_TIMER`（對索爾設 255） |
| 5 | `+0x27` | 封魔咒術（不能施法） | 封魔咒術、鬼動死靈陣 |

計時不為 0 就是生效中。每回合的遞減、中毒的扣血與麻痺的行動限制見 [`battle.md`](battle.md)；麻痺同時讓單位不能反擊（`fdps_check_can_counter_attack`（`0x137e0`））。

`fdps_unit_apply_status_effect`（`0x28f70`）施加一個指名的效果：

1. 效果編號 `0x11`→槽 5、`0x12`→槽 3、`0x13`→槽 4；其他值直接當槽號，並把效果編號**改寫成 `0x14`**。所以神之祝福的三個槽都以法術 `14` 的命中率 100 判定，必定成功。
2. 依序判定，前一項不過就不做後一項（每一項都可能多消耗 `rand()`）：`rand() % 100 < hit_rate` → 該槽目前為 0（已生效中的不重疊、不刷新）→ 效果編號在 `0x11`..`0x13` 時目標不是免疫者。
3. 成立時計時設成 `rand() % 2 + 2`（2 或 3 回合），並 `pending_xp += target.level × 10`。

免疫者由 `fdps_unit_is_ailment_immune`（`0x29080`）判定：職業代碼 `0x19`（機兵）、`0x21`–`0x22`、`0x24`–`0x26`，或肖像編號 `0x3c`–`0x44`（`ENEMYDAT.DAT` 前九筆）。`0x23` 傭兵夾在中間但**不**免疫。免疫只擋三種異常，神之祝福不受影響。

`fdps_unit_inflict_random_ailments`（`0x28ee0`）是鬼動死靈陣命中後的附加：對槽 3、4、5 各擲一次，`rand() % 100 < 20` 且目標不免疫就把該槽設成 `rand() % 2 + 2`。它不看法術的命中率、**會覆蓋**已在倒數的計時、不計經驗；免疫判定在機率判定之後，免疫的目標照樣消耗 `rand()`。

buff 的數值由 `fdps_unit_recompute_combat_stats`（`0x24d70`）在重算戰鬥數值時套上，公式見 [`battle.md`](battle.md) 的「狀態計時器：效果、持續與解除」。

## 施法與使用道具的經驗值

`data_fdps_battle_pending_xp_credit` 是一次行動的待結算經驗。玩家施法：`fdps_battle_action_menu`（`0x15d00`）每一輪開頭把它清 0，上面三支效果函式（傷害、回復、狀態）把分數累加進去，施法結束後 `fdps_battle_spell_command`（`0x27c20`）在處理完死亡事件後做

```
lv = caster.level
if (caster.portrait_id > 8) lv += 30
pending_xp = pending_xp / lv            // 有號除法，向零截斷
```

再交給 `fdps_unit_award_exp_and_level_up`（`0x1dd30`）發放（發放與升級見 [`battle.md`](battle.md)）。`portrait_id > 8` 除了轉職後的型態（`0x0f` 以上）之外也包括 `09` 蓋亞、`0A` 珊、`0B` 蘭斯洛特，恰好是教會轉職候選條件「肖像編號 `< 9`」的補集（見 [`village.md`](village.md)）。同一個 +30 在別處用的是別的界線：回復經驗（`fdps_unit_apply_heal`（`0x27070`），上一節）是 `0x0f`..`0x21`，物理攻擊的經驗是 `肖像編號 > 10`（見 [`battle.md`](battle.md)）；三處各自獨立，不是同一條規則。神行術、傳送術、甦癒術不累加任何分數；神之祝福每成功一槽就是 `目標等級 × 10`，一次最多三槽。

使用道具**不給經驗**：`fdps_apply_item_effect_to_targets`（`0x262a0`）在結算死亡事件之前把待結算經驗清 0，行動選單在道具成功使用後也再清一次。AI 施法與 AI 使用道具同樣清 0。

## 道具的使用

行動選單的「道具」開啟 `fdps_battle_item_menu`（`0x252b0`）的四向選單：使用、交給、裝備、丟棄。

**選物品**：`fdps_unit_item_select_loop`（`0x25b20`）以「使用」開啟時只接受 `use_effect != 0` 的物品，其他物品按確認鍵沒有反應。武器也可以「使用」，只要它帶使用效果。

**瞄準**：與法術相同的兩段收集，換成物品的欄位——可瞄準範圍 `range_code = use_distance`、選取模式 = `select_mode`（`+0x15`）；確認後的名單在 `use_distance < 0x10` 時是從游標格、`range_code = use_radius`、同一個 `select_mode` 的洪水填充，`use_distance >= 0x10` 時是從施法者格往游標走 `use_distance − 0x10` 格的直線（只收陣營 0）。玩家這邊用的是 `select_mode`；AI 用的是 `use_target`（`+0x11`），見 [`map_ai.md`](map_ai.md)。`use_effect` 為 `0x1c` 時第一次收集排除自己的格，`0x1c` 與 `0x19` 另會追加一段目的格選取，但出貨的 `ITEM.DAT` 沒有任何物品帶這兩個碼，`fdps_apply_item_effect_to_targets` 也沒有對應的處理。確認後道具效果套用、單位標成已行動（`fdps_battle_mark_unit_done`（`0x119b0`）），回合結束；瞄準取消則回到物品清單，游標停在剛才選的那一列。

**效果**：`fdps_apply_item_effect_to_targets`（`0x262a0`）依 `use_effect` 分派，`amount` 是 `use_amount`（有號 16-bit）。每個碼做什麼、使用後是否消耗、哪些物品帶這個碼，見 [`../assets/items.md`](../assets/items.md) 的「使用效果代碼」；「消耗」指 `fdps_unit_remove_item` 把使用者背包裡那一格移除。那張表沒寫的實作細節：

- 元素傷害（`01`–`04` 道具、`07`–`0A` 武器）：道具版與武器版共用同一段，先播 `EMg00`／`EMg05`／`EMg08.saf`（地系改成播 `EarQu.wav`、震動 25 格，同裂地術的 `rand() % 4 − 2`），再 `fdps_apply_damage_to_targets(amount)`；傷害落下後再比一次碼，只有道具版移除物品。
- `0B`／`20` 直接交給 `fdps_apply_heal_to_targets(amount)`，動畫與閃白都在它裡面。
- `0C` 播 `CureMP.saf` 並閃白，每個目標 `mp_max == 0` 浮出 MISS，否則 `fdps_unit_restore_mp(amount)` 並浮出擲出的數字。
- `0F`–`14` 播 `Cure.saf`，分別改 `hp_max`（`hp_current` 不動）、`mp_max`、AP 基礎值（`+0x37`）、DP 基礎值（`+0x39`）、DX 基礎值（`+0x3e`）與移動力（`+0x3b`，byte）。
- `16`／`18` 清中毒（`+0x25`）／麻痺（`+0x26`）計時，原本有才浮出 CURE；清除發生在特效播放之前。
- `1E` 每個目標 `fdps_unit_apply_damage(amount)` 並浮出數字，沒有特效。
- `21`／`22` 開訊息視窗，看的是肖像編號（`+0x07`）是否 `09` 蓋亞；`23` 看的是**角色編號**（`+0x08`）是否 `08` 布蘭多。不符時三者都顯示拒絕訊息（文字 `0x21d`）、不消耗；`23` 符合但背包沒有 `A3` 金屬礦時只顯示文字 `0x220`、不消耗。
- `05`、`06`、`0D`、`1B` 沒有任何分支：什麼都不發生、不消耗，但回合照樣用掉。

- 永久強化的幅度（15、7、1、30、100）是程式裡的常數，這些物品的 `use_amount` 都是 0（見 [`../assets/items.md`](../assets/items.md)）。
- `0F`–`14`、`16`、`18` 與 `21`–`23` 只作用在名單第一個單位，不論名單多長。只有 `11`、`12`、`13`、`21` 會重算戰鬥數值。
- 永久強化的數字以「增益」字色浮出（字形基底 `0x0d`），傷害用 `0`、回復用 `0x27`。
- 分派結束後一律：待結算經驗清 0、收集死亡事件、播死亡動畫、執行死亡事件（事件的獎勵給使用者）。

## 物品欄、交付與丟棄

背包是單位記錄 `+0x0a` 起的 8 格，每格 2 byte：旗標 byte（`0x80` 空格、`0x40` 裝備中）與物品編號。

- `fdps_unit_remove_item`（`0x25cc0`）把後面的格往前移一格補洞，再把第 8 格的旗標設成 `0x80`；編號 byte 留著舊值。
- `fdps_unit_add_item`（`0x25d20`）放進第一個空格（旗標清 0，所以新放入的物品不是裝備中），8 格全滿時回 `−1`、什麼都不寫。
- `fdps_unit_item_count`（`0x25240`）數旗標沒有 `0x80` 的格。
- `fdps_unit_find_item_slot`（`0x34520`）只掃前「已佔用格數」格，回傳第一個編號相符的格。

**交給**：只有在使用者上下左右相鄰處有我方（陣營 2）單位時才可選——`fdps_battle_item_menu`（`0x252b0`）每一輪開頭探測一次，探不到就把這一項反灰，而且在這次選單裡不會再恢復。選好物品與對象後，對方背包有空位就直接轉交；對方背包全滿時改成交換：玩家再從對方背包選一件，雙方互換。直接轉交時只重算交出者的戰鬥數值（對方收到的物品不是裝備中），交換時兩人都重算。交付或交換成功後，就算玩家接著取消整個行動選單，這個單位的回合也算用掉了。

**丟棄**：選一件移除；不論有沒有丟，都重算一次戰鬥數值。

**裝備**與**丟棄**不會用掉回合：它們不改變選單的回傳值，取消行動選單後單位仍可移動與攻擊。背包空了時選單立即結束。

## 裝備

裝備畫面 `fdps_unit_equip_window`（`0x25da0`）從戰鬥的道具選單與村莊的隊伍選單都開得到（村莊端見[`village.md`](village.md)）。它反覆開物品清單讓玩家選，每選一件：

1. `fdps_unit_can_equip_item`（`0x25fe0`）查 `PROEQU.DAT` 第 `clazz` 筆（**不**加 1，這張表沒有預設列）的 6 個類型碼，物品的類型（`+0x00`）與其中任何一個相等就可以裝；6 格全比、不理會填充值 `0xFF`。不能裝時**沒有任何反應**。
2. 可以裝時播 `Equip.wav`，`fdps_unit_equip_slot`（`0x26070`）把背包裡所有與它同類、正在裝備中的物品卸下（旗標清 0），再把選中的格旗標設成 `0x40`。「同類」只看類型碼是否都 `<= 0x15`（武器）或都 `> 0x15`（其他），類型 0 的物品不會被卸下。
3. 重算戰鬥數值並重畫數值欄。

Esc 離開，或背包變成空的時離開。裝備不用掉回合。

武器與防具的查找用 `fdps_unit_find_equipped_slot`（`0x25140`）：武器是類型 `0x01`–`0x15`、防具是 `0x16`–`0x27`，回傳最低的那一格。

裝備改變後由 `fdps_unit_recompute_combat_stats`（`0x24d70`）重算 AP、DP、HIT、EV；哪些格算裝備中、公式、1.15 的截尾與名冊版的差別見 [`battle.md`](battle.md) 的「狀態計時器：效果、持續與解除」。

## 原版錯誤

- 玩家「使用」大地之劍與白銀之槍時，`fdps_battle_item_menu`（`0x252b0`）拿物品的 `select_mode` 當選取模式，而這兩件的值是 4 與 5：大地之劍可以確認，但命中名單永遠是空的，只有震動畫面、沒有傷害、回合照樣用掉；白銀之槍的瞄準游標按確認鍵永遠沒有反應，只能取消。機制見 [`known_bugs.md`](known_bugs.md) 第 10 條；白銀之槍被封住的使用效果見 [`../cut_content/items.md`](../cut_content/items.md) 的 I10（大地之劍玩家本來就拿不到，沒有內容可封）。

## 相關文件

- 法術與物品的數值：[`../assets/spells.md`](../assets/spells.md)、[`../assets/items.md`](../assets/items.md)
- record 佈局：[`../assets/tables/spells.md`](../assets/tables/spells.md)、[`../assets/tables/items.md`](../assets/tables/items.md)、[`../assets/tables/classes.md`](../assets/tables/classes.md)
- 特效容器格式：[`../resource_info/saf.md`](../resource_info/saf.md)、[`../resource_info/vfs.md`](../resource_info/vfs.md)
- 回合推進、狀態遞減、經驗發放與升級：[`battle.md`](battle.md)
- AI 的法術與道具選擇、AI 施法的演出分派：[`map_ai.md`](map_ai.md)
- 洪水填充與移動網格：[`movement.md`](movement.md)
- 重建踩雷點：[`../rebuild_info/pitfalls.md`](../rebuild_info/pitfalls.md)
