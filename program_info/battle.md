# 戰鬥數值與回合

**驗證對象**：`FDPS.LE` 的物理攻擊、經驗值與升級、狀態計時器、回合推進與單位死亡。涵蓋的 function 與所在的 `src/` 檔：`0x18d60`、`0x194e0`–`0x1a4bf`（全螢幕戰鬥畫面：`fdps_combat_play_attack_exchange`、`fdps_combat_slide_in_attacker`、`fdps_combat_play_blow`、`fdps_combat_compute_hit_outcome`、`fdps_combat_slide_backdrops`，`src/combat.c`、`src/cmbblow.c`）；`0x120d0`、`0x1c3a0`–`0x1ca99`（地圖上的攻擊與休息，`src/unitatk.c`）；`0x1dd30`、`0x1e370`、`0x1fa30`、`0x27070`、`0x275a0`、`0x27840`、`0x28460`、`0x28ee0`、`0x28f70`、`0x29080`（經驗、升級、HP／MP 增減、狀態異常與計時器，`src/unitstat.c`）；`0x119b0`、`0x12960`、`0x12b20`、`0x15470`、`0x1e3f0`、`0x2bae0`、`0x2e0c0`、`0x2ea10`（回合與階段，`src/btlturn.c`）；`0x1d6c0`、`0x1d990`、`0x26180`、`0x274e0`（死亡，`src/death.c`）；`0x109b0`、`0x12550`、`0x24d70`、`0x2d210`、`0x2db50`、`0x2df90`（單位記錄的存取與衍生數值，`src/unit.c`）；`0x15d00`（行動選單，`src/btlact.c`）。以上範圍內的戰鬥規則以此檔為唯一正典。敵方 AI 怎麼選行動見 [`map_ai.md`](map_ai.md)，法術與道具的效果見 [`spell.md`](spell.md)，移動範圍與地形讀取見 [`movement.md`](movement.md)，章節勝敗判定見[`chapter.md`](chapter.md)。

下文的「記錄 `+n`」指單位記錄（`fdps_unit_record`，0x50 byte，[`data_structures.md`](data_structures.md)）的位移：`+5` 狀態 byte、`+6` 陣營（0 敵方、1 友軍 NPC、2 我方）、`+7` 肖像編號、`+0x20` 職業、`+0x21` 等級、`+0x22`..`+0x27` 六個狀態計時器、`+0x3c` 經驗餘數、`+0x40`／`+0x42` HP／HP 上限、`+0x44`／`+0x46` MP／MP 上限、`+0x48` AP、`+0x4a` DP、`+0x4c` HIT、`+0x4e` EV。

## 兩條物理攻擊路徑

同一次物理攻擊有兩份各自獨立的實作，走哪一份由誰發動與戰鬥動畫開關決定：

| 發動者 | 動畫開關 `data_fdps_ui_battle_animation_enabled` | 路徑 |
| --- | --- | --- |
| 我方（行動選單的攻擊） | 不看 | 全螢幕：`fdps_combat_play_attack_exchange`（`0x18d60`） |
| 敵方、友軍 NPC（`fdps_map_actor_move_and_attack`（`0x12e50`）） | 非 0 | 全螢幕：同上 |
| 敵方、友軍 NPC | 0 | 地圖上：`fdps_unit_attack_target`（`0x1c3a0`），每一擊由 `fdps_unit_resolve_attack_hit`（`0x1c520`）結算 |

我方的攻擊指令在 `fdps_battle_action_menu`（`0x15d00`）裡無條件呼叫全螢幕路徑，所以關掉動畫只影響 AI 發動的交戰（包括我方單位在那場交戰裡的反擊）。兩條路徑的公式骨架相同，以下幾處不同，而且都會改變結果：

| 項目 | 全螢幕（`fdps_combat_compute_hit_outcome`（`0x19f80`）） | 地圖上（`fdps_unit_resolve_attack_hit`（`0x1c520`）） |
| --- | --- | --- |
| 六個數值欄位（AP、HIT、DP、EV、HP、HP 上限）的讀法 | 零延伸（`XOR EAX,EAX` 再 `MOV AX`），負值讀成 65535 附近 | 符號延伸 |
| 連擊判定抽幾次亂數 | 2 次 | 1 次 |
| 武器附加異常的免疫判定 | `fdps_unit_is_ailment_immune`（`0x29080`）完整判定 | 只看守方職業是否 `0x19`（機兵） |
| 給經驗的條件 | 攻方陣營 2 且守方陣營 0 | 攻方陣營 2 且守方肖像編號 ≥ `0x3c` |
| 守方 HP 何時寫回 | 動畫的命中影格逐段扣 | 結算當下一次寫回（未命中也寫回原值） |
| 附加異常與爆擊的畫面 | 爆擊的命中影格把 DAC 第 0 色設成白色 50 ms | 異常：全調色盤綠閃兩次；爆擊：白閃兩次 |

零延伸讀法的後果（DP 為 −1 等於不受傷、EV 為 −1 等於打不中）列在 [`rebuild_info/pitfalls.md`](../rebuild_info/pitfalls.md)。

## 一次攻擊的流程：連擊與反擊

### 全螢幕路徑

`fdps_combat_play_attack_exchange`（`0x18d60`）依序做：

1. 清空經驗累加器 `data_fdps_battle_pending_xp_credit`。
2. 問一次 `fdps_check_can_counter_attack`（`0x137e0`）決定要不要載入守方的攻擊動畫。
3. `fdps_combat_play_blow`（`0x196d0`）播攻方的一輪打擊，回傳守方剩下的 HP。
4. 回傳值非 0，**而且**重新再問一次 `fdps_check_can_counter_attack`（`0x137e0`）得 1，才以攻守對調呼叫 `fdps_combat_play_blow`（`0x196d0`）播守方的反擊。第二次詢問不能沿用第一次的答案：步驟 3 的打擊可能剛讓守方麻痺，麻痺的單位不能反擊。

反擊只有一輪，反擊不會再被反擊。

`fdps_combat_play_blow`（`0x196d0`）的一輪打擊：

- **連擊**：打擊數預設 1；`rand() % 100 < 3` 成立則為 2；攻方武器 `hit_effect` 為 2（雙擊）則為 2；再抽一次 `rand() % 100 < 3`，成立則為 2。兩次抽取都無條件執行，三個條件都是「設成 2」而不是累加，所以最多兩擊。非雙擊武器的實際連擊率是 1 − 0.97² = 5.91%。
- **每一擊**：先記下守方當下的 HP（零延伸）當作起點，呼叫 `fdps_combat_compute_hit_outcome`（`0x19f80`）算出這一擊的命中、爆擊與傷害，然後播攻方的 `Act%03d.saf`。攻擊動畫裡帶命中標記（影格記錄 byte `+5` 非 0）的影格數記為 n（至少 1）；播到第 k 個命中影格時守方 HP 寫成 `起點 − k × 傷害 / n`（有號整數除法，截尾向零；小於 0 取 0）。播完最後一個命中影格時扣掉的正好是全部傷害。影格格式見 [`resource_info/saf.md`](../resource_info/saf.md)。
- 某一擊結束時守方 HP 為 0，剩下的那一擊取消。

n 是整個動畫的命中標記數，連「前導影格」（第 0 格 byte `+4` 指定的前幾格，播在畫面滑向守方之前）也算在內，但前導影格不付款；命中標記若落在前導影格裡，守方只會損失播到的那一部分。出貨的 91 個 `Act%03d.saf` 不會出現這種情形：有前導影格的 7 個（`ACT005`、`ACT020`、`ACT029`、`ACT062`、`ACT093`、`ACT094`、`ACT095`）命中標記全部落在前導影格之後，而且每個檔至少有一格命中標記，所以 n 從不需要補成 1，每一擊都會付完全部傷害。

### 地圖上路徑

`fdps_map_actor_move_and_attack`（`0x12e50`）在動畫關閉時：

1. `fdps_unit_attack_target`（`0x1c3a0`）(攻方, 守方)：打擊數預設 1；抽一次 `rand() % 100 < 3` 成立則為 2；武器 `hit_effect` 為 2 則為 2。每一擊播地圖攻擊動畫、呼叫 `fdps_unit_resolve_attack_hit`（`0x1c520`）結算並寫回 HP、把血條由舊寬度一格一格降到新寬度；守方 HP 為 0 就不打下一擊。回傳守方剩下的 HP。
2. 守方剩下的 HP 非 0 與 `fdps_check_can_counter_attack`（`0x137e0`）得 1 兩者都成立，就以攻守對調再呼叫一次 `fdps_unit_attack_target`（`0x1c3a0`）；反擊的連擊判定同樣抽一次亂數。

### 反擊的條件

`fdps_check_can_counter_attack`（`0x137e0`）回傳 1（可以）或 −1（不行），四個條件依序：

1. 守方的麻痺計時器（`+0x26`）為 0。
2. 攻守兩者的 |Δx| + |Δy| **等於** 1：只有上下左右相鄰；同格與斜角都不行。
3. 守方有裝備武器。
4. 那件武器的 `range_min`（道具記錄 `+0x0b`）**等於** 1。

所以隔一格以上的遠程攻擊一律不會被反擊，最短射程為 0 或 2 以上的武器也不能反擊。AI 評分時預測反擊用的是另一支以格子座標為參數、射程條件寫成 `range_min < 2` 的函式，兩者不一致，見 [`map_ai.md`](map_ai.md)。

## 單次打擊的公式

`fdps_combat_compute_hit_outcome`（`0x19f80`）與 `fdps_unit_resolve_attack_hit`（`0x1c520`）以同樣的順序算一擊（兩者的差異見上一節的表）。A 是攻方、D 是守方；所有除法都是有號 `IDIV`，截尾向零：

```
crit   = PROMAP[A.職業 + 1].critical               # 職業記錄 byte +8，零延伸
weapon = ITEM[A 的裝備武器]                        # hit_effect = +9，rate = +0x0a，皆零延伸

if A 不是飛行單位: AP += T_ap[地形(A)] * AP / 100   # 先乘後除
if D 不是飛行單位: DP += T_dp[地形(D)] * DP / 100

if weapon.hit_effect == 3: crit += weapon.rate     # 爆擊武器：加到爆擊率，不另抽
elif weapon.hit_effect == 4: 中毒判定（見下節）
elif weapon.hit_effect == 1: 麻痺判定（見下節）

if rand() % 100 < HIT - EV:                        # 先算有號差，再比
    if rand() % 100 < crit:
        DP = DP / 2                                # 截尾向零；爆擊只動 DP
    dmg = (AP - DP) * 9 / 10
    if dmg < 0: dmg = 0
    s = dmg / 9
    if s != 0: dmg += rand() % s                   # 加成 0 .. s-1
    HP = max(HP - dmg, 0)
else:
    dmg = 0                                        # 未命中
```

- 地形類別由 `fdps_map_load_tile_info`（`0x2ba00`）依單位所站的格填入；兩張修正表的值與類別 6 讀到表外的問題見 [`resource_info/terrain.md`](../resource_info/terrain.md)。
- 飛行單位由 `fdps_unit_is_flying`（`0x12550`）判定：職業 `0x16`、`0x17`、`0x18`、`0x1f`、`0x25` 五個寫死的代碼，飛行單位不吃也不受地形修正。
- 命中：HIT − EV ≥ 100 必中、≤ 0 必不中。`rand()` 的值域是 0..32767，32768 不是 100 的倍數，餘數 0..67 各出現 328 次、68..99 各 327 次，命中率因此比 (HIT − EV)% 略高一點點。
- 職業爆擊率、武器的附加效果與機率見 [`assets/classes.md`](../assets/classes.md)、[`assets/items.md`](../assets/items.md)。
- 攻方的武器是 `fdps_unit_find_equipped_slot`（`0x25140`）找到的裝備格。攻方沒有裝備武器時那支函式回 −1，`fdps_unit_get_item_id`（`0x25200`）接著把記錄 byte `+9` 當物品編號讀，沒有任何檢查。我方沒有武器時攻擊指令反灰、反擊也要求有武器，所以只有沒帶武器的 AI 單位發動攻擊時才會走到這裡。

## 武器附加的狀態異常

武器 `hit_effect` 為 4（中毒）或 1（麻痺）時，在**命中判定之前**：

```
if rand() % 100 < weapon.rate 且 D 不免疫:
    中毒: D.計時器[3] = rand() % 4 + 2             # 2..5
    麻痺: D.計時器[4] = rand() % 2 + 2             # 2..3
```

- 判定在命中擲骰之前，所以**這一擊就算沒打中，異常照樣可能附上**。
- 計時器是直接指派：已經中毒的單位被再次命中時，剩餘回合數換成新擲出的值（可能變短）。
- 免疫判定：全螢幕路徑呼叫 `fdps_unit_is_ailment_immune`（`0x29080`）；地圖上路徑只比對守方職業是否等於 `0x19`。

`fdps_unit_is_ailment_immune`（`0x29080`）在以下任一成立時回 1：職業 `0x19`（機兵）、`0x21`..`0x22`（守護獸、將軍）、`0x24`..`0x26`（？？、惡靈、活屍），或**肖像編號** `0x3c`..`0x44`（`ENEMYDAT.DAT` 的前九筆）。`0x23`（傭兵）夾在兩段之間而不免疫，`0x1a`（魔神）與 `0x27` 也不免疫。職業名稱見 [`assets/classes.md`](../assets/classes.md)。

法術施加的狀態（`fdps_unit_apply_status_effect`（`0x28f70`）與鬼動死靈陣的 `fdps_unit_inflict_random_ailments`（`0x28ee0`））寫的是同一組計時器，持續回合數都是 `rand() % 2 + 2`；施加條件與免疫判定的順序見 [`spell.md`](spell.md)。

## 狀態計時器：效果、持續與解除

六個計時器（記錄 `+0x22`..`+0x27`）都是「剩幾次倒數」，非 0 就算生效：

| 槽 | 位移 | 狀態 | 效果 |
| ---: | --- | --- | --- |
| 0 | `+0x22` | 神之祝福（攻擊） | AP 乘 1.15 |
| 1 | `+0x23` | 神之祝福（防禦） | DP 乘 1.15 |
| 2 | `+0x24` | 神之祝福（敏捷） | DX 加 15，因此 HIT 與 EV 各加 15 |
| 3 | `+0x25` | 中毒 | 每次己方階段開始扣 HP 上限的 1/10；不能休息 |
| 4 | `+0x26` | 麻痺 | 各階段都跳過它；不能反擊；不算在「我方還有人沒行動」裡；不能休息 |
| 5 | `+0x27` | 封魔咒術 | 行動選單的法術指令反灰 |

**衍生數值**由 `fdps_unit_recompute_combat_stats`（`0x24d70`）重算：

```
AP  = ap_base(+0x37) + Σ 裝備中物品.ap
DP  = dp_base(+0x39) + Σ 裝備中物品.dp
dx  = dx_base(+0x3e) + (計時器[2] ? 15 : 0)
HIT = dx + Σ 裝備中物品.hit
EV  = dx + Σ 裝備中物品.ev
if 計時器[0]: AP = (int)(AP * 1.15)                # x87 double 乘法後截尾
if 計時器[1]: DP = (int)(DP * 1.15)
```

- 「裝備中」是八個背包格裡所有旗標 byte 帶 `0x40` 的格，不限兩件；三個基礎值與物品的四個修正都是有號 16-bit，物品欄位的位置見 [`assets/tables/items.md`](../assets/tables/items.md)。四個結果以 16-bit 寫回，超出範圍就截斷。
- 1.15 是 double 常數 `0x3FF2666666666666`，比 1.15 略小，乘完截尾：AP 100 變 114、200 變 229，不是整數算術的 115、230（踩雷點見 [`../rebuild_info/pitfalls.md`](../rebuild_info/pitfalls.md)）。
- buff 乘在「基礎 + 裝備」的總和上，不是只乘基礎值。
- 名冊版的 `fdps_roster_recompute_combat_stats`（`0x23ac0`）做同樣的基礎值與裝備加總，但不套任何 buff。

**倒數與中毒傷害**由 `fdps_battle_tick_status_effects`（`0x1fa30`）(陣營) 在每個陣營階段開始時執行（時機見「回合的各階段」）：

1. 該陣營每個中毒（計時器 [3] 非 0）且未退場的單位：`HP = HP − HP上限 / 10`（兩個欄位零延伸讀），小於 0 取 0；播 `PosEff.saf` 與扣血數字。中毒可以致死。
2. 呼叫 `fdps_play_death_animation_and_mark_dead`（`0x1d6c0`）結算 HP 為 0 的單位，再呼叫本章的行動後處理函式（中毒致死因此可以直接結束戰鬥）。
3. 該陣營每個未退場單位的六個計時器，非 0 的各減 1；減到 0 就呼叫 `fdps_unit_recompute_combat_stats`（`0x24d70`）把到期的 buff 拿掉。

中毒的傷害在倒數之前，所以計時器為 n 的中毒會扣 n 次血。麻痺的倒數也在該陣營行動之前，所以計時器為 n 的麻痺讓單位錯過 n − 1 個己方階段。buff 在施放當下的那個階段剩餘時間之外，再維持 n − 1 個己方階段。以法術或道具提前解除異常的方式見 [`spell.md`](spell.md)。

## 經驗值

經驗先累加在全域 `data_fdps_battle_pending_xp_credit`，行動結束時一次付給一個單位。

### 物理攻擊

每一擊結算時（全螢幕與地圖上兩條路徑的條件見第一節），若符合給經驗的條件：

```
L    = A.等級                                      # byte
if A.肖像編號 > 10: L = (L + 30) & 0xff             # byte 加法，會繞回
base = D.等級 * ENEMYDAT[D.肖像編號 - 0x3c].exp_reward / L   # 三者皆零延伸
credit = (這一擊後 D 的 HP == 0) ? base : base * dmg / D.HP上限
```

- 這是**指派**不是累加：一場交戰裡最後一個符合條件的打擊決定整場的經驗。連擊時第一擊沒打死、第二擊只擦傷，經驗只按第二擊的傷害比例算；第二擊落空則是 0。
- 「打死」看的是這一擊之後的 HP；未命中時 HP 不變，credit 是 `base × 0 / HP上限` = 0。
- 肖像編號 > 10 的判定涵蓋 `0x0b` 蘭斯洛特與所有轉職後的形態（`0x0f` 起）；除數為 0（等級 0）或守方 HP 上限為 0 時是除以零，沒有防護。
- 交戰結束後 `credit = credit * 15 / 10`，再交給 `fdps_unit_award_exp_and_level_up`（`0x1dd30`）。收款人：我方的攻擊指令是攻擊者本人（`fdps_battle_action_menu`（`0x15d00`））；AI 發動的交戰是**被攻擊的那個單位**（`fdps_map_actor_move_and_attack`（`0x12e50`）），我方單位就是靠敵方回合裡的反擊拿經驗。攻方與反擊方都不是我方時，credit 維持 0，收款函式在入口就返回。
- `exp_reward` 的數值見 [`assets/enemies.md`](../assets/enemies.md)。守方陣營 0 而肖像編號小於 `0x3c` 的單位會讓全螢幕路徑讀到 `ENEMYDAT.DAT` 之前的記憶體，見 [`rebuild_info/pitfalls.md`](../rebuild_info/pitfalls.md)。

### 法術、回復與道具

法術的傷害（`fdps_unit_apply_damage`（`0x28460`））、回復（`fdps_unit_apply_heal`（`0x27070`））與狀態（`fdps_unit_apply_status_effect`（`0x28f70`））對累加器是**累加**（`+=`），一次施法打到幾個目標就累加幾次，與物理攻擊的指派相反；三者各加多少、施法後怎麼除以施法者等級見 [`spell.md`](spell.md)。使用道具不給經驗：道具效果 `fdps_apply_item_effect_to_targets`（`0x262a0`）在結算死亡事件前把累加器清 0，行動選單 `fdps_battle_action_menu`（`0x15d00`）在道具成功使用後再清一次，都不付款；AI 施法與 AI 使用道具同樣清 0 不付款，見 [`spell.md`](spell.md#施法與使用道具的經驗值)。

### 付款與升級

`fdps_unit_award_exp_and_level_up`（`0x1dd30`）(unit)：

1. credit 為 0、單位已退場、或單位等級**等於**上限（肖像編號 9 的蓋亞是 99，其他是 40）時直接返回，**不清累加器**。
2. credit 大於 99 就改成 99。
3. `running = credit + 經驗餘數(+0x3c)`；地圖上浮出 credit 的數字。
4. `running ≥ 100` 時升一級：等級 +1、學新法術、`running −= 100`、五項屬性成長、重算衍生數值、顯示升級視窗（最多 250 格，按鍵可提前結束）。接著若（肖像編號 9 且新等級 99）或新等級為 40，`running = 0`。
5. `running` 以 byte 存回經驗餘數，累加器清 0。

credit 最多 99、餘數不到 100，所以一次付款最多升一級。步驟 4 的歸零條件第二半沒有排除蓋亞，他升到 40 級時餘數也會被清掉，雖然他能繼續升到 99（原版 bug，見[`known_bugs.md`](known_bugs.md)）。

## 升級時的屬性成長

每升一級，`fdps_unit_award_exp_and_level_up`（`0x1dd30`）依 AP、DP、DX、HP 上限、MP 上限的順序，各呼叫一次 `fdps_level_up_apply_stat_gain`（`0x1e370`）：

```
(min, max) = FRILEVUP[肖像編號] 的對應兩個 byte    # 零延伸
gain = min
if max - min != 0: gain = min + rand() % (max - min)
欄位 = (short)(欄位 + gain)                         # 16-bit，不設上限
```

- `max` 與 `min` 不同時是**不含**的上界：可能的成長是 `min`..`max − 1`。
- `max == min` 時不呼叫 `rand()`，不消耗亂數。
- 寫入的欄位是 `ap_base`（`+0x37`）、`dp_base`（`+0x39`）、`dx_base`（`+0x3e`）、HP 上限（`+0x42`）、MP 上限（`+0x46`）。**目前 HP 與 MP 不增加**。
- 學法術：成長記錄 byte `+0x0a` 是 `GETMGTAB.DAT` 的索引（`0xff` 表示不學），該記錄的六組（等級, 法術編號）中等級等於新等級的每一組都以 `fdps_set_flag_bit`（`0x282b0`）設進法術位元圖（`+0x1a`），並在視窗裡顯示法術名。

成長表與法術習得表的數值見 [`assets/characters.md`](../assets/characters.md)（表上印的上限是檔案原值，上下限不同時實際擲不到）。

## 共用亂數

戰鬥的每一個機率判定都取自 Watcom CRT 的 `rand`（`0x42cf8`）：

```
seed = seed * 0x41C64E6D + 0x3039                  # 32-bit，自然繞回
return (seed >> 16) & 0x7FFF                       # 0..32767
```

種子初值為 1，`srand`（`0x42d1a`）沒有任何呼叫者。但**計時器中斷每個 tick 都抽掉一個值**：`fdps_timer_tick_handler`（`0x30790`）由 AIL 以每秒 25 次呼叫，每次把 tick 計數器加 1 並呼叫一次 `rand()` 丟掉結果；這支處理函式不管有沒有音效卡都會註冊。所以亂數序列本身固定，但戰鬥裡某個判定拿到序列中的哪一個值，取決於計時器啟動以來經過了多少 tick——玩家操作的快慢會改變結果，讀檔重來也會得到不同的擲骰（攻略站「利用存取檔來達到較佳的屬性」的建議成立的原因）。

戰鬥相關的抽取點與次數：

| 時機 | 函式 | 次數 |
| --- | --- | --- |
| 全螢幕的一輪打擊開始（攻方與反擊各一輪） | `fdps_combat_play_blow`（`0x196d0`） | 2（連擊） |
| 地圖上的一輪打擊開始 | `fdps_unit_attack_target`（`0x1c3a0`） | 1（連擊） |
| 每一擊 | `fdps_combat_compute_hit_outcome`（`0x19f80`）、`fdps_unit_resolve_attack_hit`（`0x1c520`） | 異常武器 1，附上時再 1；命中 1；命中後爆擊 1；傷害 ≥ 9 時加成 1 |
| 升級 | `fdps_level_up_apply_stat_gain`（`0x1e370`） | 五項中上下限不同的各 1 |
| 單位陣列搬移 | `fdps_relocate_unit_array`（`0x2df90`） | 每次 1；敵方階段第一趟與友軍階段的每個索引（不論是否可行動）各一次、我方單位回合迴圈的每一圈一次 |
| HP 回復、MP 回復、法術／道具傷害 | `fdps_unit_apply_heal`（`0x27070`）、`fdps_unit_restore_mp`（`0x275a0`）、`fdps_unit_apply_damage`（`0x28460`） | 各 1 |
| 法術狀態異常 | `fdps_unit_apply_status_effect`（`0x28f70`） | 1，成功時再 1 |
| 鬼動死靈陣的隨機異常 | `fdps_unit_inflict_random_ailments`（`0x28ee0`） | 3，每附上一項再 1 |
| 法術命中與震動畫面 | `fdps_spell_damage_unit`（`0x28320`）、`fdps_cast_spell_on_targets`（`0x288f0`）、`fdps_apply_item_effect_to_targets`（`0x262a0`） | 見 [`spell.md`](spell.md) |
| 狀態視窗（允許待機動畫時） | `fdps_unit_status_window_wait_input`（`0x170c0`） | 每開一次 1 |

地圖 AI 的評分與決策本身不呼叫 `rand()`。戰鬥外另有 `fdps_transition_random_blocks`（`0x2fb80`）與 `fdps_run_bonus_lottery`（`0x36460`）兩處抽取。

## 回合的各階段

回合計數器 `data_fdps_battle_turn_counter` 在進章節時設為 1。一個回合是「我方階段 → 友軍階段 → 敵方階段 → 下一個我方階段」。

### 我方階段

`fdps_battle_player_phase_loop`（`0x2bae0`）每一格畫面讀一次按鍵：方向鍵移動游標；ESC、Z、數字鍵 5、Delete 把游標移到下一個還能行動的我方單位（`+5 & 0x85` 為 0 且陣營 2，不看麻痺）；Enter／Space 在游標下的單位上：

- 陣營 2、`+5` 的 bit 7 未設、麻痺計時器為 0：進入 `fdps_battle_unit_turn`（`0x15470`），結束後若戰鬥未結束再呼叫 `fdps_battle_end_phase_if_all_units_done`（`0x2ea10`）。
- 其他單位：只開狀態視窗。
- 沒有單位：開系統選單 `fdps_battle_system_menu`（`0x14ab0`）。

`fdps_battle_end_phase_if_all_units_done`（`0x2ea10`）在沒有任何陣營 2 的單位滿足「`+5 & 0x81` 為 0 且麻痺計時器為 0」時呼叫 `fdps_battle_advance_turn`（`0x1e3f0`）。中毒但還沒行動的單位會讓階段繼續，麻痺的不會。系統選單的「結束回合」直接呼叫 `fdps_battle_advance_turn`（`0x1e3f0`）；「全軍前進」把每個可行動的我方單位往游標處移動並標成已行動，再呼叫同一支。

`fdps_battle_unit_turn`（`0x15470`）是一個單位的回合：顯示移動範圍、選目的地、走過去、開 `fdps_battle_action_menu`（`0x15d00`）。移動過後除了 `char_id` 7 與 3 以外，法術指令反灰。移動後在行動選單按取消會把單位放回原地重選（控制這件事的旗標 `data_fdps_battle_action_cancel_ends_turn_flag`（`0x60004`）沒有任何寫入，「取消即結束回合」的分支走不到，見 [`../cut_content/code.md`](../cut_content/code.md) 的 C5）。在範圍游標上按取消則不花掉這個單位的回合。行動選單回報完成之後，`fdps_battle_unit_turn`（`0x15470`）以 `fdps_battle_mark_unit_done`（`0x119b0`）把單位標成已行動。移動範圍與路徑見 [`movement.md`](movement.md)。

### 回合推進

`fdps_battle_advance_turn`（`0x1e3f0`）依序：

1. **閒置單位休息**：陣營 2、`+5 & 0x81` 為 0（本回合沒行動）、未中毒、未麻痺、HP 不**等於** HP 上限的單位，`HP += HP上限 / 5`（有號截尾），大於上限則設為上限；以白色剪影閃一格，有人休息就播 `REST.WAV`。我方階段自然結束時每個可行動的單位都已標成已行動，所以這一步休息的是以系統選單「結束回合」提早收掉階段時還沒行動的單位。
2. **友軍**：回合事件（陣營 1）、狀態倒數（陣營 1）、友軍階段。
3. **敵方**：「敵方回合」字卡、清除所有單位 `+5` 的 bit 7、回合事件（陣營 0）、狀態倒數（陣營 0）、敵方音樂、敵方階段。
4. **下一個我方階段**：回合計數器 +1、「我方回合」字卡、清除 bit 7、我方音樂、回合事件（陣營 2）、狀態倒數（陣營 2）。
5. **每回合 MP 回復**（見下節）。
6. 游標回到單位 0（除非它已退場）。

步驟 2、3 的狀態倒數與階段之後各檢查一次戰鬥結束碼 `data_fdps_chapter_event_or_battle_end_code`，非 0 就立刻返回；步驟 4 之後不檢查。回合事件由 `fdps_battle_run_turn_events`（`0x2e0c0`）掃 `MAPnn.DAT` 的 16 筆回合事件，格式與觸發時機見 [`resource_info/map.md`](../resource_info/map.md)。

友軍階段在清除 bit 7 之前執行；友軍上一次行動時設的 bit 7，已在那之後的敵方字卡與我方字卡兩次清除中被清掉。

### 敵方與友軍階段

- `fdps_battle_enemy_turn_phase`（`0x12960`）掃兩趟單位陣列。可行動的條件：陣營 0、`+5 & 0x81` 為 0、麻痺計時器為 0。第一趟每個索引都先搬移單位陣列，可行動者的最佳法術或最佳道具評分達到 6 才行動；第二趟不評分，還能行動的全部行動。結果是有好法術或好道具可用的敵人先動，其餘的後動，每個敵人都恰好有一次行動。
- `fdps_battle_npc_turn_phase`（`0x12b20`）只掃一趟、不評分，陣營 1 的可行動單位依索引順序行動。
- 兩者在每個索引之後（不論有沒有行動）：若有待處理的格子事件就以該單位索引呼叫；呼叫本章的行動後處理函式；戰鬥結束碼非 0 就立刻返回。

行動本身（`fdps_map_actor_behavior_step`（`0x10010`））與評分見 [`map_ai.md`](map_ai.md)。事件處理函式追加部署的單位會接在陣列尾端，因為每一圈都重讀單位數，同一階段就輪得到它們。

## 單位記錄 +5 狀態 byte

| 位元 | 意義 | 寫入 | 讀取 |
| --- | --- | --- | --- |
| bit 0（`0x01`） | 已退場 | 多數是整個 byte 指派為 1（其他位元一併清掉）；過場腳本的 `0x0B` 退場以 OR 1 設、`0x0C` 復活以 AND `0xfe` 清，只動這一位（見[單位死亡與退場](#單位死亡與退場)） | `fdps_unit_is_retired`（`0x109b0`）只看這一位 |
| bit 7（`0x80`） | 本回合已行動 | `fdps_battle_mark_unit_done`（`0x119b0`）以 OR 設；`fdps_units_clear_status_bit7`（`0x2db50`）以 AND `0x7f` 清 | 地圖上改畫成已行動的影格（`fdps_draw_map_unit`（`0x2cda0`）） |
| bit 2（`0x04`） | 沒有意義，恆為 0 | 沒有任何寫入會設它（見表下） | 只出現在 `0x85` 遮罩裡 |

bit 2 恆為 0：映像中對 `+5` 的寫入只有整 byte 指派 0 或 1（過場腳本的閃爍退場指派 `p & 1`）、OR `0x80`、OR 1、AND `0x7f`、AND `0xfe`、AND 1。單位陣列的記錄只來自 `fdps_deploy_unit`（`0x232b0`）（`+5` 設 0）、`fdps_build_map_unit_array`（`0x22be0`）（複製名冊後設 0，空 slot 設 1）與讀檔；名冊記錄也只由 `fdps_roster_add_character`（`0x23bc0`）（設 0）、`fdps_roster_write_back_battle_units`（`0x23980`）（`&= 1`）、`fdps_roster_revive_fallen_members`（`0x39e70`）（設 0）與讀檔寫入。存檔裡的值都是遊戲自己寫出去的，所以這一位從來沒有機會變成 1，`& 0x85` 與 `& 0x81` 在原版的任何可達狀態下結果相同。

各處的測試方式不同，不能互換：

- `& 0x81`：各階段的「可行動」、我方階段是否結束、閒置休息。
- `& 0x80`：我方階段按 Enter 時只看這一位。
- `& 0x85`：我方單位循環鍵與「全軍前進」。
- `== 0`（整個 byte）：每回合 MP 回復。
- `& 0x01`：退場判定、死亡腳本收集、狀態倒數、找游標下的單位（`fdps_battle_find_unit_at_cursor`（`0x2daa0`））。

bit 7 每回合在敵方字卡後與我方字卡後各清一次。退場單位不畫、不被游標找到、不參與任何階段，但仍留在陣列裡佔一個索引。

## 休息與每回合的 MP 回復

**休息指令**：行動選單的第四格在單位本回合沒移動時先呼叫 `fdps_unit_rest`（`0x120d0`），再搜尋腳下的格子、標成已行動。`fdps_unit_rest`（`0x120d0`）在 HP 不**等於** HP 上限、未中毒、未麻痺時 `HP += HP上限 / 5`（有號截尾），超過上限則設為上限，閃白並播 `REST.WAV`。AI 的休息呼叫同一支（[`map_ai.md`](map_ai.md)）。回合推進時的閒置休息見[回合推進](#回合推進)。

**MP 回復**（`fdps_battle_advance_turn`（`0x1e3f0`）步驟 5）：

- 單位陣列索引 4：背包有 `0xa6` 妖刀村正，沒有才找 `0xa7` 妖刀正宗，找到的那一件要是裝備中。
- 單位陣列索引 8：背包有 `0xb1` 形見指環且裝備中。
- 每個單位：背包有 `0xb3` 魔精石碎片（不必裝備）。

另外都要求 `+5` 整個 byte 為 0、MP 不等於 MP 上限。符合就呼叫 `fdps_unit_restore_mp`（`0x275a0`）(unit, 15) 並浮出數字；同時符合前兩項之一與魔精石碎片的單位回復兩次。依 `fdps_unit_restore_mp`（`0x275a0`）的擲法（[`spell.md`](spell.md)），要求 15 點時擲出的是 `15 × 9 / 10 + (rand() % 100) × 15 / 1000`，即 13（餘數 < 67）或 14，夾到 MP 上限。物品資料見 [`assets/items.md`](../assets/items.md)。

## 單位死亡與退場

**死亡**：HP 正好為 0 的未退場單位，由 `fdps_play_death_animation_and_mark_dead`（`0x1d6c0`）處理：原地轉 13 格（朝向依格數 mod 4），接著把 `+5` 整個設成 1，再在它們身上播 `Explo.Saf` 爆炸。這支在每次攻擊交戰、施法、道具使用、狀態倒數與部分章節腳本之後被呼叫。判定是 `HP == 0`，不是 `HP ≤ 0`。

**死亡腳本**在死亡動畫**之前**收集、之後執行：

| 情境 | 收集 | 執行（擊殺者參數） | 經驗付款相對順序 |
| --- | --- | --- | --- |
| 我方攻擊 | `fdps_collect_death_scripts`（`0x26180`） | 攻擊者 | 付款在執行腳本之前 |
| AI 攻擊 | `fdps_collect_death_scripts`（`0x26180`） | 被攻擊的單位 | 付款在執行腳本之後 |
| 我方施法 | `fdps_collect_death_scripts`（`0x26180`） | 施法者 | 見 [`spell.md`](spell.md) |
| AI 施法 | `fdps_collect_death_script_events`（`0x274e0`），只收 opcode 2..5 | AI 攻擊評分留下的目標索引（[`map_ai.md`](map_ai.md)） | 不付款 |
| 道具使用（我方與 AI） | `fdps_collect_death_scripts`（`0x26180`） | 使用者 | 不付款 |

收集條件是未退場、HP ≤ 0、有腳本；由 `fdps_run_death_scripts`（`0x1d990`）逐筆執行。opcode 的意義、「掉物品與金錢要求擊殺者是存活的我方單位，否則整串剩下的都不執行」的規則見 [`resource_info/map.md`](../resource_info/map.md)。opcode 4、5 把戰鬥結束碼設成 2（過關）或 1（敗北）；其餘的勝敗由章節的行動後處理函式判定，見[`chapter.md`](chapter.md)。

**退場**是 `+5` 的 bit 0 為 1 的狀態，死亡只是其中一條路。死亡、AI 行為 7 走到目的地時（[`map_ai.md`](map_ai.md)）與章節腳本（[`chapter.md`](chapter.md)）都把整個 byte 指派成 1，連 bit 7 一併清掉；過場腳本（[`cutscene.md`](cutscene.md)）的 `0x09` 閃爍退場同樣是整 byte 指派，單一單位的 `0x0B` 退場與 `0x0C` 復活卻是 OR 1／AND `0xfe`（`fdps_icon_script_run` 內的 `0x21b11`），只動 bit 0。章節腳本也會把整個 byte 寫回 0 讓單位重新出場。`fdps_unit_mark_retired`（`0x138f0`）是整 byte 指派的函式版本，原版映像中沒有任何引用，各處都是內嵌寫法（重建的 `src/mapai.c` 把行為 7 的內嵌處寫成呼叫它）。

## 相關文件

- 本頁提到的原版 bug 的機制：[`known_bugs.md`](known_bugs.md)。
- 重建時照直覺寫就會偏離原版的項目：[`rebuild_info/pitfalls.md`](../rebuild_info/pitfalls.md)。
- 單位記錄在映像中的位置與型別：[`data_structures.md`](data_structures.md)。
