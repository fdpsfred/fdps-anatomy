# 票 25.15：刪減調查順帶發現的錯誤修正

票上六條、加上其他子票轉來的四條。每條都先重新查證再改，修正項目都是已確定的事實、數量少，所以逐條親手處理，沒有寫 workflow。

## 1. `architecture.md` 的 36 支走不到的 function

票上說「這 36 支沒有一支在章節分派表裡」。先用 `src/chapter.c` 確認：四張表 30 + 50 + 30 + 30 = 140 項全部是具名的 `fdps_` 初值，這一半很快成立。

麻煩在「36 支是哪 36 支」。刪減調查的程式碼 agent 給的 36 支是「`src/` 裡零引用」的清單，和 `architecture.md` 說的「呼叫圖走不到」是兩個不同的集合，數字相同只是巧合：零引用清單裡有 DPMI 包裝 5 支與 `fdps_cd_status_is_not_busy`，呼叫圖卻走得到它們；反過來呼叫圖走不到 `fdps_unit_mark_retired`、`fdps_keyboard_isr`、`fdps_timer_tick_handler` 以及三支只被其他死成員呼叫的 CD helper，零引用清單沒有它們（前兩者在 `src/` 裡有取址，後三者被死碼呼叫）。差點直接拿零引用清單去重寫段落。

改用 `workspace/call_graph/graph.json` 與 `tools/call_graph/analyze_graph.py` 的 BFS 重算。只從 LE 進入點出發：reached 790、unreached 555，其中 `fdps` 恰好 36 支；再把全部 297 個指標表目標當起點：reached 980、unreached 365，`fdps` 35 支——少掉的那支是 `fdps_transition_slide`，它自己內部的跳躍表被當成指標表、把自己當成了起點，是假可達。兩種算法的 `fdps` 集合因此一致，就是 36 支。

**沒有收斂的一件事**：`architecture.md` 第 7 行與第 148 行的總數（1,008 可達、337 走不到、ail 203、crt 98、「325 個指標表進入點」）兩種算法都重現不出來（790／555 或 980／365，297 個不同的目標）。`fdps` 的 36 兩邊都對得上，所以本票只重寫 `fdps` 那一段，總數留給 25.17 的逐條驗證。

36 支的分類：`-oe` 展開後的本體 7（其中 `fdps_spell_heal_unit` 原本的註解說是「沒人用的包裝」，25.9 的 C12 判定與 `src/spell.c` 自己在 `fdps_cast_spell_on_targets` 裡的「expanded here」註解都說它是展開後留下的，順手把 `src/` 與 Ghidra 的說法改掉）、以位址掛上的中斷處理常式 2、通用程式庫未用成員 22（MSCDEX 19 + VFS 3）、前作遺留 1、刪減痕跡 4。中斷處理常式這一類是票上沒列的：它們執行期會跑，只是呼叫圖不追資料取址。

後來 `CONTEXT.md` 補了兩條分類規則（遊戲自己模組裡沒被呼叫的成員歸殘留內容、FDPS 改寫過的前作程式也歸殘留內容），我原本寫的「通用程式庫 22、前作遺留 1、刪減痕跡 4」三格有兩格因此站不住——MSCDEX 與 VFS 包裝是遊戲自己的模組，`fdps_load_indexed_archive_entry` 依 25.9 的 C10 是改寫過的。`architecture.md` 改成只分「展開後的本體／中斷處理常式／完全沒有呼叫者」三類，後一類歸哪一種刪減與未用交給 `cut_content/` 判定，不在這裡重複下結論。

同頁「仍未定的」最後一條「整批留給票 17」已經過時（`data.txt` 裡有 232 個 `data_fdps_` 符號、沒有 `DAT_`），刪掉並在涵蓋範圍一節補一句「plate comment 裡的 `DAT_xxxxxxxx` 只是位址記法」——comments 裡仍有一千多行這種寫法，這件事本身是真的。

## 2. `FMer*.tmp` 的讀回

四處讀回（`combat.c`、`cmbspell.c`、`church.c`、`ending.c`）都確認了，而且四處都在結束時讀回 `Mer1/2.tmp`。原註解的推理錯在只看了 `00061dc0`／`00061dd0` 這兩個字面值的 xref——其他四個讀者各自帶一份同名字面值（Ghidra 裡是 `0x61690`、`0x61f64` 等另外的字串）。`src/main.c`、`src/main.h` 之外，Ghidra 的 `0x29660` plate 與 `0x61dc0` 字面值的 plate 也都寫了同樣的錯誤結論，後者甚至在 Rebuild note 裡說「這兩個寫入看起來是死的」，一起改。這個「只看字面值 xref 就判定沒人讀」的陷阱記進 `pitfalls.md`。

## 3、6. `icon.c` 的 opcode `0x61` 與呼叫點數

`0x21650` 的 xref 數了一次：30 支 init、第 8 章事件 3 處、33 處 end（第 18、27、30 章各兩處），共 66。巨集改名成 `SCRIPT_OP_AWARD_XP_UNIT_3`，與 `resource_info/cutscene_script.md` 的 opcode 名 `AWARD_XP_UNIT_3` 一致。Ghidra 的 `0x21650` plate 沒有「67」也沒有「除錯」的說法，不用改。`chend2b.c`／`.h` 說隱藏路線的 end 處理函式「沒有寫回」是對的，補上一句「寫回由 `WinGA26.dat` 的 `0x61` 做」讓兩邊對得起來。

## 4. 物品名

用 `tools/text_decode` 解 `FDETXT00.TXT` 第 `0xC9 + n` 條，逐筆對 `assets/items.md` 的 226 筆：恰好三處不同，與票上說的一致（`50` 火神砲、`CC` 空字串、`D5` 空之寶石）。中途發現物品 `FF` 讀到的正是法術 `0A` 裂地術的名稱，本來想寫進 `items.md`，但 25.6 的 `global_text.md` 已經擁有這件事，改成連過去。`src/item.h`、`src/item.c` 的註解也用了攻略站名稱，一併改。技能資料集 `fdps_data.json` 是從 `items.md` 產生的，用「HEAD 版本的其他表 + 我的 `items.md`」在 scratch 重建，只差三個名稱與 `name_source`，避免把 25.7 未 commit 的表混進來。

## 5. `FRIAPRDA.DAT` 的「樣板列」

這條是真的錯，而且比票上說的更廣。解出 `FDETXT00.TXT` 第「索引 + 1」條的單位名：`0C` 索爾、`0D` 卡里斯、`0E` 亞雷斯、`23`「？？？？」、`3B` 侍衛。再掃 64 張地圖的部署記錄裡角色編號在 `0C`–`3B` 的：`0C` 15 筆、`0D` 2、`0E` 4、`23` 3、`24`–`27` 26、`3B` 4，其他索引一筆都沒有；入隊呼叫只傳 `00`–`0B`，標題示範戰另外傳 `0C`。

拿攻略站核對：索爾第 1–6 章 LV10 的 HP 969／MP 489／AP 710／DP 120／DX 170 與第 26 章 LV40 全部吻合（FRILEVUP `0C` 十個成長值都是 1）；侍衛 LV40 與「？？？？」的 LV17／18／40 三次出場也全部吻合——`3B` 與 `23` 原本在 `characters.md` 標「攻略站未列」，其實攻略站列了它們的戰場數值，只是沒有列成長表。

`0D` 與 `0C` 逐 byte 相同，但它是卡里斯自己的索引：第 13 章的敵方演員，開場腳本在玩家看到地圖前就讓他退場（`src/chinit1b.c` 早就寫了）。`15`／`1F` 的職業、配備、法術與 `0B` 蘭斯洛特相同但基礎值較高，沒有任何讀取端；攻略站的 LV15 蘭斯洛特套公式對上的是 `0B`。它是什麼（刪減還是填充）留給 25.11 判定。

`src/title.c` 的 `DEMO_CHAR_TEMPLATE` 其實是索爾，改名 `DEMO_CHAR_SOL`（沿用 `chpost1.c` 已有的 `Sol` 拼法）；`src/chinit2.c`／`.h` 把第 17 章的人質與第 23 章的群眾稱為「template rows」、`src/chpost3.c` 稱索爾那一列為「template」，都改掉。Ghidra 裡沒有同樣的說法。

寫到一半發現 25.7 同時在改 `characters.md`（加入表、轉職路線）與技能資料集，而且已經把我的段落接進去了。兩邊的改動交錯在同一批檔裡，commit 時只放我自己的 hunk。

## 其他轉來的項目

- `resource_info/cel.md` 說地圖 31 是腳本切得過去的正常場景：`cutscene_script.md` 的追蹤說沒有腳本切到 31，`terrain.md` 說沒有 `DSC31.DAT`；再看 `src/rsrc.c`，載入順序是 `map`→`dtl`→`dsc`→每層 `mpl`／`cel`／`attr`，而 `m%02d%d.cel` 全程式只有這一個載入點。所以 `M310.CEL` 在出貨的遊戲裡根本不會被載入，繪製器讀不懂它的編碼不構成矛盾。`open_issues.md` 那一條問的是「地圖 31 畫出來正不正常」，答案是「它永遠不會被畫」，這個靜態證據足以回答，收掉。`pitfalls.md` 的 `0x0D` 那一列與 Ghidra 的 `fdps_cel_header.pixel_encoding` 欄位註解都改成這個結論。
- `resource_info/text.md` 的前作字對照表：HEAD 版本已是「不能按索引直接套」，確認無誤。

## 25.9 跑完之後追加的六條

工作途中 API session 上限打斷了一次，換帳號後接著做。回來時 HEAD 已經多了 25.6、25.7、25.9 的 commit，重新盤點工作目錄，確認沒有別人動到 `src/`。

- `src/chevt3.h` 說第 18 章 wave 1／2「沒有任何出貨資料會要」：用 `tools/cutscene_script` 解 `ICON17.DAT`，`0x18a` 是 `DEPLOY_WAVE 2`（記錄 32，`0x2a4` 又讓它退場）、`0x2f1` 是 `DEPLOY_WAVE 1`（記錄 0–14 全部），是開場敵軍。註解改正；Ghidra `0x38150` 的 plate 沒有這個說法。
- `assets/classes.md` 把職業 `27` 與 `1C`／`1E`／`23` 並列為刪減與未用：`PROMAP.DAT` 第 40 列與 `24` 那列逐 byte 相同（`ff×8 00 64`），`PROEQU.DAT` 216 byte 只有 36 筆，確認它是表尾列，拆開寫。
- `src/aiscore.c` 的 `AND AL,0x80 at 0001337d`：Ghidra 的指令列表是 `0001337d MOV AL,[EAX+0x34]`、`00013380 AND AL,0x80`，改成 `00013380`。`src/chevt5b.c` 與十幾個 plate 寫的「`fdps_score_targets_for_item` (0001337d) test」指的是讀那個 byte 的位址，與 `00012c6f` 成對，沒改。
- `resource_info/map.md` 說 31 號以後的 `MAP` 檔「帶著與 `MAP00` 相同的格子事件與可搜尋格記錄」：用 `map_decode` 逐張比，格子事件表除 `MAP51`、`MAP56` 外確實與 `MAP00` 相同，但可搜尋格記錄沒有一張與 `MAP00` 相同——多數共用另一份，`MAP51`、`MAP56` 各自不同。事件碼層在 31 以後全是 0 這半句成立。
- 字串起點早一個 byte 的 10 個定義：用指令立即值逐個找真正的起點，結果不全是「早一個 byte」：`0x61ac9`、`0x61ad9` 早了 3 byte（`\n F`、` %s` 是填充），`0x61d82` 早了 2 byte。全部改成從真正起點開始的 `string`，第一次用 `TerminatedCString` 建，快照裡與鄰居型別不一致，又改回 `string`。改完之後再掃一次遊戲側字串池（`0x61550`–`0x62230`），沒有「起點沒有引用、+1／+2 有引用」的定義了。`pitfalls.md` 那一列原本拿 `0x61827` 當現行例子，改成「遊戲側已校正、其他區仍可能出現」；`0x61dc0` 的 Rebuild note 還說「Ghidra 的物件是 `0x61dbd` 起的 13 byte 字串」，其實早就是 `char[10]` 了，一併改。
- `src/chevt6.c` 說「帶號的偏移才讓第 7 回合落在波次 3」：7 / 2 帶號與否都是 3，決定行為的是截斷；帶號只在計數器為負時有差，而回合計數器從 1 起算。改成截斷是行為、帶號是原版寫法。

## 回歸閘的 `different`

第一次跑完整閘門，`game` 報 `different`。照規則當回歸查：開一個 `HEAD` 的 worktree（`fdps_game_files` 用 junction 接過去）建一份，**`HEAD` 本身也是 `different`**——基準值是票 24 記的，之後 25.3 改過 `maptile.c`／`mapai.c` 的註解，閘門從那時就紅了，只是沒人跑完整閘門。

逐 byte 比 `HEAD` 與我的建置：272 byte、148 段，全在 object 2 的開頭。146 段都是字面值結尾 NUL 之後、補到 4 byte 邊界的 1–3 byte，內容像 `ailfile`／`nemfile`、`ux`／`of` 這種文字碎片——`wcc386` 不清零對齊填充，填的是編譯器緩衝區的殘值，會跟著原始碼文字變。剩下 2 byte 在 `_DATA` 的 `data_fdps_ui_play_active_flag`（`unsigned char`，`+0x1549`）與下一個陣列（`+0x1550`）之間，同一回事。第一版比對腳本只認得字面值後的填充，把這 2 byte 報成可疑，查 `FDE.MAP` 與 `gamedata.c` 的宣告才確認；所以正式的 `tools/build_gate/pad_diff.py` 對非字面值的差異改成列出兩側的 map 符號讓人判斷，而不是自動放行。

再往下追「為什麼 `HEAD` 也紅」：把 worktree 切到基準值自己的 commit `6abf243` 重建，結果與 worktree 裡的 `HEAD` 建置**完全相同**（0 byte 差異），卻仍然與基準值不同。再把我的 `src/` 改動複製進 worktree 建一次，與 worktree 的 `HEAD` 只差 20 byte（全是填充）；而主工作目錄的建置與 worktree 的 `HEAD` 差 272 byte。差別在換行字元：worktree 由 git 取出是 CRLF，主工作目錄裡被 agent 用 LF 直接寫過的檔還是 LF，填充的殘值跟著變。也就是說，這個基準值從記下之後在任何別的工作目錄狀態都重現不了，閘門紅是填充造成的，不是 25.3 或我改壞了什麼。確認全部是填充後推進 `game` 的基準值，理由寫明比對結果；並在 `build_gate.md` 記下這個效果與辨認方法。根本的解法是讓閘門依 map 抹掉對齊空隙，這次沒有做。

另外，第一次跑閘門時我的前一次閘門（被 session 中斷前啟動的）還在背景跑，兩個閘門共用 `workspace/code_emit`，把兩個都殺掉重跑一次才得到乾淨的結果。
