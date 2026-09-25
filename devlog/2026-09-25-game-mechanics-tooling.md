# 票 25.5 第一段：遊戲機制頁的工具與 workflow

票 25.5 要在 `program_info/` 補齊「這個遊戲怎麼運作」的頁面。這一段只做了不需要逐項判斷的部分：閘門、轉錄、索引三支 Python 工具與整支 workflow 腳本。頁面本身還一頁都沒寫。

## 為什麼停在這裡

這張票由主 session 派給一個子 agent 做，而子 agent 手上沒有 Workflow 工具——ToolSearch 查不到，25.9 也先撞到同一件事。票面寫明「逐項工作由 workflow 一檔一個 agent 產生草稿」，CLAUDE.md 的鐵則又要求清單只能活在 workflow 腳本裡。手上其實有 Agent 工具，可以一頁派一個子 agent 出去，形式上也是「一個 agent 一個項目」；但那樣清單就活在我的 context 裡而不是腳本裡，續跑、整輪全滅的偵測、收尾報告都得靠我自己記，正是 ADR-0007 要消滅的東西。主 session 隨後也明講不要這樣繞。所以決定：腳本寫好放進 `tools/game_mechanics/`，由主 session 代跑，跑完再回來收尾。

## 頁面怎麼切

照票面列的七項加上 `known_bugs.md`，再做兩個調整：

- 戰鬥拆兩頁。`battle` 寫數值與回合（`combat.c`、`cmbblow.c`、`unitatk.c`、`unitstat.c`、`btlturn.c`、`death.c`⋯），`map_ai` 寫敵方 AI（`mapai.c`、`aiscore.c`、`aitarget.c`、`aiact.c`）。前作把 AI 塞在 `battle.md` 裡，但 FDPS 這邊光 AI 的四個檔就超過十萬 byte，一個 agent 同時扛兩邊，context 會先被原始碼吃掉。
- 多一頁 `movement`。票面沒列移動範圍，但「遊戲怎麼運作」少了它說不過去，前作也有 `pathfind.md`。代價只是多一個 agent。

`known_bugs` 是唯一需要柵欄的頁：它彙整其他每一頁草稿回報的原版 bug，所以要等全部起草完。每一條 bug 仍由它自己對照組語複核，起草者的筆記只當線索。

## 閘門踩到的東西

`check_mechanics.py` 用 `--selftest` 的內建測試寫，照 `data_emit/check_data.py` 的慣例——這台機器的 Python 沒有 pytest，而本專案的工具本來就沒有外部測試框架。

寫好之後拿既有的兩頁試跑，兩頁都不過：

- `architecture.md` 有兩處 `` `fdps_battle_system_menu`（`00014c9c`） `` 這種寫法，括號裡是 function **內部**某條指令的位址，不是進入點；閘門把括號形式一律當成「這個 function 在這個位址」，所以報不符。考慮過讓閘門接受落在 function body 內的位址，但那樣「每條規則都標出所在 function」這個檢查就分不出「引用 function」與「指某條指令」。最後把寫法定死：括號形式只放進入點，指令位址寫成「`` `name` `` 內的 `` `0x…` ``」。舊頁不歸這張票改。
- 同一頁第 174 行有「票 17」，是真的流水帳殘留。這條留給 25.15。
- `cd_audio.md` 的每一段都只寫裸位址、不寫名稱，閘門的「每段至少一個引用」全部報錯。那是這支閘門的規則比舊頁嚴，不是舊頁錯。

流水帳用語的清單原本放了 `phase`，寫到一半發現戰鬥頁一定會談「回合的各階段」，`btlturn.c` 自己的註解就叫 turn and phase engine，而函式名稱裡也有 phase。清單改成只收明確描述分析經過的詞（「一開始以為」「後來發現」、ISO 日期、票號、devlog），`phase` 拿掉並補一條測試確認戰鬥階段不會誤報。

## 審查抓到的洞

寫完第一版之後派了兩個審查 agent，一個對規範、一個對票面。抓到的東西大多是真的，照改：

- **草稿互相連結會被閘門擋。** 閘門把草稿當成已經放在 `program_info/` 來解析連結，但兄弟頁在落地前都還不存在，prompt 叫起草者連 `known_bugs.md`、`spell.md`，閘門卻回 broken-link，等於逼起草者把連結刪掉才能過。改成草稿模式下，連到 `program_info/` 同層、而草稿資料夾裡有同名檔的連結算通過；落地之後的閘門仍然照真實路徑檢查，兄弟頁被拒收時就會抓到。
- **驗證對象的寫法跟本資料夾對不上。** 既有頁全是 `**驗證對象**：…` 粗體段，閘門第一版只認 `驗證對象` 開頭或 `## 驗證對象` 標題，會把本資料夾自己的慣例判錯。prompt 也改成要求照本資料夾的粗體段寫、寫涵蓋的位址範圍。
- **判斷與落地沒分開。** 第一版的踩雷點與索引 agent 一邊判斷一邊直接改 `pitfalls.md`／`_index.md`，違反 ADR-0007 第二條，改完也沒有閘門。改成判定 agent 只寫判定檔，新增 `apply_kb.py` 逐字套用，套用後跑連結閘門。`apply_kb.py` 第一次拿真檔測才發現 `pitfalls.md` 是 CRLF、`_index.md` 是 LF——以 `\n` 切行時每一行尾巴都帶著 `\r`，節標題永遠比對不到。改成依原檔的換行字元切與接，補了測試。
- **重跑會重複加列。** 索引 agent 被要求「新增」列但拿到的是所有已落地頁（含 unchanged），重跑就重複。`apply_kb.py index` 改成該頁已有列就不動。
- **回掃可能把乾淨的第一版蓋掉。** 回掃 agent 若改到過不了閘門，第一版就沒了，`land.py` 會整頁拒收。改成回掃前先備份、改壞就還原，並在中繼資料記 `rescanned`，重跑不會再掃一次答不出來的題目。
- **`cut_content/` 的連結會變成孤兒。** 起草者若自己猜 `../cut_content/lottery.md` 這種路徑，等那個資料夾建好、檔名不同時就成了斷鏈，而沒有任何東西負責回來修。改成只准連 `../cut_content/_index.md`，更深的路徑閘門直接報錯；每頁回報的被封住內容候選與待生效連結都放進 run record，交給建 `cut_content/` 的工作。
- **停止之後的略過項目沒有全列。** Ghidra 失去回應造成的停止、重試輪整輪全滅、依序判定的上游失效，第一版都有漏記或不偵測。補齊，並讓每個被略過的階段都在未完成清單留一行。
- 第 28 章援軍波次那條種子候選，擁有者原本寫成 `program_info/chapter.md`，但那是各章內容，歸章節頁；改掉。
- 票號的流水帳規則會誤判遊戲文字裡的「彩票 3 張」，改成前面緊接中文字的不算。

沒採納的：selftest 的 `expect` 骨架在三支腳本裡各寫一份——本專案的工具慣例是 self-contained，只有「已有擁有者的機制」才 import，測試骨架不算。

另外主 session 轉來存檔格式那邊的發現：標題選單的「讀檔」「繼續」要求 live-state 章節 byte 不是 `0xff`，而存讀檔畫面新建的檔整份是 `0xff`，所以只在章節之間存過 slot 的玩家下次開機進不了讀檔畫面。這條放進 `known_bugs.md` 起草者的線索清單，由它親自複核後決定收不收；`save` 頁的範圍也補上 `title.c` 與標題選單的條件。

## 沒辦法驗的部分

這台機器沒有任何 JavaScript 引擎（node、deno、bun 都沒有），workflow 腳本只能用一支臨時的 Python 小程式檢查括號與樣板字串的配對，再靠人讀；真正的剖析要等主 session 第一次呼叫 Workflow。四支 Python 工具的內建測試全數通過，閘門的 `--links` 對現有的 `pitfalls.md` 與 `program_info/_index.md` 也是乾淨的。
