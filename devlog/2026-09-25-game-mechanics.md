# 票 25.5 第二段：遊戲機制頁的 workflow 實跑

上一篇（`2026-09-25-game-mechanics-tooling.md`）只寫好工具與 workflow 腳本，頁面一頁都沒有。這一段由主 session 代跑 `tools/game_mechanics/mechanics_ticket25_5.js`，從起草一路跑到踩雷點與索引的套用。中途撞到 session 用量上限，使用者換帳號後讓 workflow 接著跑完。run record 裡 `halted` 是 `false`、`haltReason` 空白，所以就 record 看得到的範圍，中斷沒有讓哪一個階段被標成停止。至於哪幾個 agent 當時正在跑、有沒有被重派，record 沒有分開記，這裡就不猜。

機器原始記錄在 `devlog/runs/2026-09-25-mechanics-t255-01.json`，以下是讀那份 record、`workspace/game_mechanics/drafts/*.meta.json` 與 `workspace/game_mechanics/pitfalls/*.json` 之後整理出來的經過。

## 起草：九頁加 `known_bugs`

要求的九頁全部交出草稿，信心度全都是 high：

| 頁 | 未決問題 | 回報的原版 bug | 踩雷點候選 |
| --- | --- | --- | --- |
| `battle` | 0 | 4 | 10 |
| `map_ai` | 3 | 5 | 15 |
| `movement` | 0 | 0 | 9 |
| `spell` | 2 | 1 | 6 |
| `chapter` | 0 | 4 | 6 |
| `cutscene` | 1 | 1 | 11 |
| `dialog` | 1 | 0 | 3 |
| `village` | 1 | 2 | 11 |
| `save` | 0 | 2 | 4 |

所有頁都起草完之後，`known_bugs` 才開始寫（前一篇說過它需要柵欄）。它收了 19 條 bug、3 個未決問題、2 個踩雷點候選，閘門乾淨，而且跑的時候 Ghidra 一直有回應。

## 未決問題與回掃

有未決問題的頁都回掃過一次。`chapter`、`save` 沒有未決問題，中繼資料裡 `rescanned` 是 `false`，沒有回掃。回掃後各頁的未決數與起草時**完全一樣**（`map_ai` 3、`spell` 2、`cutscene` 1、`dialog` 1、`village` 1），record 看不出回掃有沒有收掉哪一題。能確定有進展的只有 `known_bugs`：回掃的附註說頁面沒改，但 srand 那題收掉了（計時器每個 tick 會抽一次 `rand`，依據是 `battle.md` 與 `src/audio.c`），鍵盤環形佇列那題問得更精確，剩下的指示器溢出與決鬥 heap byte 兩題仍然沒答案。

留下來的題目，照中繼資料逐條列：

- `map_ai`：
  1. 起草時 `known_bugs.md` 還不存在，頁面只能寫純文字檔名，等它存在之後應改成相對連結。
  2. `resource_info/map.md` 的「AI 行為」表與本頁的行為代碼表重疊（包括行為 5 的開箱流程）。依一事一主，`map.md` 應只留欄位來源與出貨統計，再連到 `map_ai.md`，但起草者沒有權限改 `map.md`。
  3. 光束軌跡掃過最下一列之後，游標模式 6 的寫入會落到移動網格區塊之後；緊接在後面的 heap 物件是什麼，取決於執行期的配置順序，沒有確認。
- `cutscene`：同樣是 `known_bugs.md` 只能寫純文字檔名的問題。
- `spell`：
  1. 施法者等級除數的 +30 門檻是肖像編號 > 8，連還沒轉職的 09 蓋亞、0A 珊、0B 蘭斯洛特都會吃到；回復用的是 0x0f..0x21，物理攻擊用的是 > 10。> 8 是不是原意，從程式碼判斷不了。
  2. 大地之劍（select_mode 4）與白銀之槍（select_mode 5）是故意不讓玩家使用，還是資料填錯，判斷不了。頁面只記行為。
- `dialog`：轉職改過肖像的名冊成員，會不會在不在當前地圖單位陣列裡的時候透過 -0x11 碼說話，才會露出轉職前的臉。要回答這題，得知道每章名冊的人數與順序，而這又取決於選擇性加入的角色。
- `village`：章節索引 25 的村莊比對暗號時，表外那一列裝的是看板選單框架指標（EBP）的兩份複本，那是 DOS/4GW 執行期的線性堆疊位址，靜態分析得不到。所以到底能不能用某組按鍵進神秘商店，沒有用實機或除錯器確認，頁面依攻略站寫成進不去。另外，保護模式下硬體中斷如果與程式共用堆疊，`_nfree` 寫入之後、比對之前發生的中斷也可能改寫那一槽，同樣沒確認。
- `known_bugs`：
  1. 裂地術／封神裂震一次超過 50 個跳字（例如 MAP25 開場）時，指示器佇列確定會越界寫到游標與其後 25 個全域，但玩家實際看到什麼（跳字亂掉、金錢被改、當機）沒有定論，所以沒上頁面。
  2. 第 19／24 章的決鬥：`fdps_unit_is_retired(0x4d / 0x52)` 讀到陣列外的那個 byte 是 heap 內容。頁面寫的結果（打不完／挑戰者認輸）是依程式路徑加上攻略站記錄的結果，沒有靜態或模擬讀出那個 byte。
  3. 鍵盤環形佇列沒有滿的檢查。沒人讀的時候排進十個 make code，寫入索引就繞回讀取索引，十個全部消失。`fdps_menu_cursor_input_loop`、`fdps_message_window_wait_key`、`fdps_save_slot_select_loop`、`fdps_village_signboard_menu` 讀鍵前不清佇列，所以動畫或文字播放時先按的鍵可能十個一組地丟掉。這算不算玩家看得到的缺陷沒有決定，暫不上頁面。

寫這篇的時候對了一下落地後的頁：`program_info/cutscene.md` 第 142、206 行與 `program_info/map_ai.md` 第 241 行仍是純文字的 `` `known_bugs.md` ``。雖然 `known_bugs.md` 已經落地，這兩頁的連結沒有人回頭補，那兩題「改成相對連結」的未決問題還是開著。

## `land.py`：十頁全部收下

十頁（九頁加 `known_bugs`）全部以 `new` 落地，落地後閘門 0 個錯誤。

有 8 個待生效連結警告，全都指向 `../cut_content/_index.md`：`spell` 第 299 行、`cutscene` 第 208 行、`village` 第 117 與 316 行、`known_bugs` 第 5、111、177、187 行。那個資料夾還不存在，歸票 25.10 建立。前一篇為了不讓起草者自己猜深層路徑，規定只能連到 `_index.md`，這次這條規定實際擋住了猜路徑，但代價就是這 8 個連結要等 25.10 才會生效。

## 踩雷點判定：99 條

每條候選一個 agent，判定結果：新增 68、已涵蓋 12、只補連結 11、未達門檻而拒收 8。

### 拒收的 8 條與理由

這 8 條的事實本身都在 Ghidra 確認屬實，拒收的原因都是「照直覺寫也不會錯」或「只在一支 function 內、已記在它的註解」：

- `award-early-return-keeps-credit`：入口的檢查確實跳過歸零，上限比較也確實是等號。但每條會累加的路徑都先歸零，唯一會連續給兩次經驗的呼叫端（opcode 0x61）每次呼叫前都先指派 99，所以每個出口都歸零也不會改變任何看得到的經驗值。
- `destroy-enemies-16bit-hp-store`：16 位元寫入與迴圈外只呼叫一次死亡掃描都屬實。但重建版用結構的 short 欄位本來就對；候選主張的「最大 HP 為 0 的還原」也不會發生，因為第 24 章在掃描前先補血。這是單一 function 的細節，`src/btlend.c` 已經記了。
- `ai-spell-cast-distance-raw`：`0x13420` 把法術 `+0x03` 原封不動傳給擴散。這是單一 function 的細節，已寫在該 function 的註解；出貨的 AI 單位也沒有任何一個會直線型法術，照規則解碼不會改變看得到的行為。
- `ai-spell-death-script-killer`：`0x13c90` 與 `src/aiact.c:305` 屬實，但只在一支 function 內、plate comment 已記。依 `map_ai.md`，出貨的 AI 法術沒有一個到得了唯一會讀那個參數的 handler，所以不符合跨 function 的門檻。
- `ai-item-bag-walk-loop-counter`：迴圈走 `0..fdps_unit_item_count-1` 而不重檢旗標，這點屬實。但加入與移除都讓背包保持緊密，照直覺寫「走滿八格、跳過空格」拿到的物品集合與格位完全一樣。**附帶的問題**：`src/aiscore.c:253` 的註解寫著「bag has a hole in it (rebuild_info/pitfalls.md)」，引用的是一條這次決定不收的列，這個引用現在懸空了。
- `slot-cursor-back-step-plus-two`：往回一格寫成 `(slot + 2) % 3` 屬實，但原版只是做了正確的 0..2 循環，任何正確的重建都會重現。寫成 `(slot - 1) % 3` 是一般的 C 餘數 bug，不算偏離原版的特殊行為。
- `slot-select-extra-frame-after-decision`：`0x24650` 決定之後多畫一幀屬實。但確認與 Esc 都不移動游標，直接從按鍵分支 return，高亮一樣停在選定的格上，差別只有一個 tick 的高亮動畫與調色盤相位。這是單一 function 的細節，`program_info/save.md` 與 `src/save.c` 都已經記了。
- `mp-cost-no-afford-check-no-floor`：`0x285c0` 與 `0x288f0` 內的減法確實沒有防護。但玩家選單與 AI 法術評分（`src/aiscore.c` 第 568 行）施法前都要求 `mp_current >= mp_cost`，所以扣除時加防護或夾到 0 永遠不會觸發，加了行為也一樣。

### 已涵蓋的 12 條

- `cutscene-opcode-0x61-not-debug`：已經有 opcode 0x61 的列，連到 `resource_info/cutscene_script.md`。
- `message-close-rebuilds-scene`：行為屬實，但陷阱只在一支 function 內。依 `pitfalls.md` 自己的規則，這種陷阱該寫在 plate comment，而它的 Rebuild note 與 `src/msgwin.c` 都已經寫了。
- `secret-code-row-is-chapter-minus-one`、`secret-code-match-position-is-global`：既有列已涵蓋。
- 其餘 8 條是 `link-*`，要連的列已經有 `known_bugs.md` 連結（包括同一輪由別條判定新增、本身就帶連結的列）。

### 寫這篇時發現的重複列

對照判定檔時發現兩條判定互相矛盾：`password-table-index-minus-one` 判定「沒有任何既有列涵蓋暗號表」，於是新增一列；`secret-code-row-is-chapter-minus-one` 卻判定「第 41 行的既有列已涵蓋」。拿 `git show HEAD:rebuild_info/pitfalls.md` 對照，這一輪開跑前 `fdps_check_secret_code_key` 那一列就已經存在。所以前一條判定是錯的，現在 `rebuild_info/pitfalls.md` 第 41 行與第 54 行是講同一件事的兩列（第 54 行是這一輪加的，只連 `village.md`）。record 沒有記到這件事，是寫這篇時才看到的。兩個 agent 各自讀同一份 `pitfalls.md` 卻做出相反的判斷，第一條多半是讀漏了。這個重複列還沒處理。

## `apply_kb.py`：踩雷點 4 個錯誤，另外抓到一個重跑會重複加列的 bug

索引步驟 0 錯（10 列插進 `program_info/_index.md`），連結閘門 0 錯。踩雷點步驟報了 4 個錯誤，訊息全是「the row changed since it was read」，也就是判定檔裡的 `old_line` 在檔案裡找不到：

- `link-physical-xp-last-blow-only`、`link-animation-off-ailment-immunity`、`link-stale-skip-save-prompt-flag`：這三條要補連結的列，是同一輪另外三條判定（`physical-xp-credit-assigned-not-added`、`two-physical-resolvers-not-mergeable`、`title-skip-save-prompt-set-on-cancel`）才要新增的。判定檔依 ID 排序套用，`link-*` 排在前面，套用時那三列還不存在，所以被拒；三列後來插進去，都沒有 `known_bugs` 連結。判定 agent 被告知「更早的判定」時看得到待加的列，套用順序卻沒有照判定的先後，這是 workflow 設計上的洞。
- `item-aim-select-mode-vs-use-target`：這條是真的不符。寫這篇時把 `old_line` 與 `pitfalls.md` 第 42 行逐字比對，差在正典欄結尾：判定檔寫的是 `…(../cut_content/items.md)；`src/item.c` 的註解 |`，檔案裡只有 `…(../cut_content/items.md) |`。判定 agent 抄既有列的時候多抄了一段原文沒有的字。

套用的 agent 另外抓到 `apply_kb.py` 本身的 bug：同一批裡如果有一條連結指向同批新增的列，踩雷點步驟就**不是冪等的**。重跑時，連結先把新增的列 X 改寫成帶連結的 N，接著「新增」的檢查找不到 X，又插一份 X，所以每重跑一次就多一份。

這個 bug 是踩到才發現的。套用 agent 想把被截斷的輸出拿回來，又跑了兩次踩雷點步驟，其中一次是 PowerShell pipe，JSON 解析卡在 BOM 上失敗，但 `apply_kb` 已經寫檔了。結果三列各多出兩份，一份帶連結（N）、一份不帶（X）。它用一支機械式、有斷言保護的腳本把 `rebuild_info/pitfalls.md` 還原成第一次套用後的狀態：把第 56、156、165 行帶連結的複本改回判定的原文，刪掉多出來的 6 行（57、58、174–177），沒有做內容判斷，也沒動任何判定檔。之後在記憶體裡重新套一次，結果是 1 個錯（item-aim）、3 次重連、3 次重插，重現了這個重複 bug。**`apply_kb.py` 修好之前，不能再跑踩雷點這一步。**

寫這篇時查了檔案：那三列在 `pitfalls.md` 裡各只出現一次，新增 68 列、改動 7 列，與 68 條新增、11 條補連結減去 4 個錯誤對得上。

## 索引列與一條被這批頁面推翻的敘述

索引 agent 寫了 10 列到 `workspace/game_mechanics/index_rows.json`，再由 `apply_kb.py index` 插進 `program_info/_index.md`。它另外指出一條現在不成立的敘述：`README.md` 第 17 行寫「一檔對應一個子系統與 `src/` 模組」，`program_info/_index.md` 第 3 行寫「一個檔對應一個子系統，之後也對應一個 `src/` 模組」。`known_bugs.md` 是橫跨十幾個 `src/` 檔的專題，`battle.md` 與 `spell.md` 各自涵蓋好幾個 `src/` 檔，`spell.md` 只負責 `unitstat.c`、`unit.c` 裡的部分 function，`movement.md` 把 `maptile.c` 的一部分交給 `resource_info/map.md`。它建議改成「一檔一個子系統或跨子系統的專題，可橫跨多個 `src/` 檔」。這兩處 workflow 都不會去改，還沒處理。它也確認 `rebuild_info/_index.md` 沒有被推翻的地方。

## 原版 bug 與被封住的內容

各頁共回報 19 種原版 bug（`known_bugs` 全部收錄，其他頁依主題各自收一部分）。會封住內容的有三條：`item-use-select-mode-4-5`（大地之劍、白銀之槍在玩家手上用不出來）、`bonus-lottery-prize-uninitialized`（酒館抽獎永遠只發藥草）、`secret-code-table-chapter-25-out-of-range`（第 26 章前的神秘商店進不去）。

被封住內容的候選有 15 筆，交給建 `cut_content/` 的票 25.10，依頁分：

- `battle`：移動後取消即結束回合的規則分支（旗標 `0x60004` 映像中沒有任何寫入）。
- `chapter`：事件 handler slot 2 `fdps_chapter_event_set_game_over`、wave 0xFF 的部署記錄。
- `cutscene`：opcode 0x08 播 wav、兩支沒有呼叫端的轉場。
- `save`：`FDE.SAV` 的第四個章節 slot。
- `spell`：兩把武器的使用效果、四個沒有分支的使用效果碼、效果碼 0x19／0x1c 的目的格選取、沒有呼叫端的 `fdps_spell_heal_unit`。
- `village`、`known_bugs`：抽獎大獎與 `SHOP25.DAT` 神秘商店的貨，各頁重複列了一次。

## 還沒收掉的事

record 的 `unfinished` 只有一項，就是上面 `apply_kb.py` 那一段，拆開來是：

1. `apply_kb.py` 踩雷點步驟的非冪等 bug 要先修（連結指向同批新增列時，重跑會重複插列），套用順序也要改成先新增後連結，或讓連結能對到同批新增的列。
2. `link-physical-xp-last-blow-only`、`link-animation-off-ailment-immunity`、`link-stale-skip-save-prompt-flag` 三條連結還沒套，要套的文字是各自判定檔裡的 `new_line`。
3. `item-aim-select-mode-vs-use-target` 的 `old_line` 與實際列不符（多了 `；`src/item.c` 的註解`），要以實際列為準重做這條連結（補 `program_info/spell.md`）。

寫這篇時另外看到、record 沒有列進 `unfinished` 的：

4. `pitfalls.md` 第 41 與第 54 行重複（暗號表），第 54 行是 `password-table-index-minus-one` 誤判新增的。
5. `src/aiscore.c:253` 引用 `pitfalls.md` 裡一條拒收的列，引用懸空。
6. `program_info/cutscene.md`、`program_info/map_ai.md` 對 `known_bugs.md` 仍是純文字，沒有連結。
7. `README.md` 第 17 行與 `program_info/_index.md` 第 3 行「一檔對應一個子系統與模組」的敘述要改寫。
8. `map_ai` 提出 `resource_info/map.md` 的 AI 行為表與 `map_ai.md` 重疊，要收成一事一主。
9. 8 個指向 `../cut_content/_index.md` 的待生效連結，等票 25.10。
10. 各頁留下的未決問題（`map_ai` 3、`spell` 2、`cutscene` 1、`dialog` 1、`village` 1、`known_bugs` 3），回掃之後數量沒變。
