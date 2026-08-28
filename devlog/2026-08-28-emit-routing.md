# 2026-08-28 票 21.5：把 514 支 function 排進 76 個檔

## 先確認這張票不該用 workflow

第一個判斷是流程層的。專案的鐵則是逐項判定不得批次，強制機制是「工作清單只存在於 workflow 腳本裡、每次 agent 呼叫只帶一項」。但票 21.5 自己就寫明它不是逐項判定工作——子系統歸屬要看整體 call graph 才定得出來，切成一個 function 一個 agent 只會得到互相矛盾的分組。所以這次沒有寫 workflow，整張票由我自己讀完 514 支的名稱、行數與呼叫關係後一次規劃。

這件事值得記下來，因為前面十幾張票都是 workflow，慣性會讓人直接開一支。

## 資料從哪來

需要的是「每支 function 的語意、大小、鄰居」與「每個資料符號的取用者」。手上已有的東西：

- `ghidra_snapshot/functions.txt` 有位址、大小、tag、prototype，但沒有反編譯行數
- `workspace/logic_naming/dump/dec/` 有 514 個反編譯檔，但那是票 15 時代的狀態，票 17 的 struct 套用之後行數會變
- `workspace/call_graph/graph.json` 有邊，但名字是票 12 時代的

三份都不夠新也不夠齊，所以寫了 `tools/code_emit/DumpRoutingInputs.java`：一趟匯出每支 `pool_fdps` function 的反編譯行數、caller／callee（含 pool）、碰到的具名資料與字串，以及每個具名資料符號的取用函式清單。16 秒跑完，514 支合計 49,998 行反編譯碼。

一開始還想在這支 script 裡順便標記全域是不是初始化過的，用 Ghidra 的 `MemoryBlock.isInitialized()`。跑之前就發現不行：LE 映像在 Ghidra 裡四個 block 全都 `init=true`，`.object2` 同時裝著 `CONST/_DATA` 和 `_BSS`，Ghidra 的 block 分不出來。票 17 的 `DumpGlobalState.java` 是拿 `_edata` / `_end` 兩個常數硬切的，所以這裡把那個 Java 改動退掉，改在 Python 端用同樣兩個常數判段。多繞了一次，但至少沒有把一個永遠回 true 的欄位寫進輸出檔。

## 分組的三輪

**第一輪**用名稱前綴寫規則，449/514 命中，剩 65 支沒有規則涵蓋。這 65 支正是名字不說明落點的那些，一支一支看 plate comment 加 caller／callee 決定。有意思的幾個：

- `fdps_map_find_chest_cell` 名字在地圖層，但唯一 caller 是 AI 的 behavior step，它存在的理由就是回答「箱子在哪」——歸 AI。
- `fdps_play_death_animation_and_mark_dead` 和 `fdps_run_death_scripts` / `fdps_collect_death_scripts` 三支是同一條管線的頭中尾，但名字分屬 play / run / collect 三族，任何前綴規則都會把它們拆開。開了 `death.c`。
- `fdps_prompt_two_choice` 有十幾個散在各子系統的 caller。照「跟 caller 走」會無解，照「它是什麼」則很清楚：一個自帶輸入迴圈的 modal 視窗，和訊息視窗同族。
- `fdps_saf_play_over_background` 一度想放進 `anim.c`，因為 `anim.c` 收的就是動畫播放。後來改回 `saf.c`：它是 SAF 播放器本身，`anim.c` 是驅動它的人，把播放器搬走會讓 `saf.c` 只剩解析而沒有播放。

**第二輪**看行數，十幾個檔超過 1000 行的預算。多數拆法是明擺著的（RLE 十四支照「基本／旋轉／換色／混色」拆成四檔），少數不是：

- `battle.c` 3413 行，25 支。試過先切成三檔仍然超標，最後是把它整個解散：回合引擎、選單、勝敗三檔，加上把 `battle_item_menu` 送去 `item.c`、`battle_spell_command` 送去 `spellmnu.c`、兩支量表函式送去 `gauge.c`、`battle_move_unit_toward` 送去 `movegrid.c`。名字帶 `battle_` 不代表屬於戰鬥引擎，這是這輪最大的教訓。
- 章節事件 handler 4924 行想切五檔（每檔約 985），怎麼切都有一檔超標，因為章節是不可分的最小單位而第 8 章一章就 395 行。改切六檔，邊界刻意不平均。
- 最後一個超標的是 `btlturn.c` 1054 行。把 `fdps_battle_tick_status_effects` 移到 `unitstat.c` 解決——這支本來就是在跑中毒之類的每回合狀態，和套用／解除狀態的程式碼同族，回合引擎只是一回合叫它一次。

**第三輪**是資料。232 個 `data_fdps_*` 照「誰讀它」分：97 個只有一個檔讀，135 個跨檔。跨檔的全部進 `gamedata.c`。

這裡踩到一個判定錯誤：章節的四張 dispatch 表照規則會散到三個檔，其中 `chapter_end_handler_table` 因為只有 `main.c` 讀而單獨落在 `main.c`。四張表指向的 handler 分佈在六個章節檔裡，是同一組介面，拆開沒有道理。加了資料側的 override 把四張一起放進 `chapter.c`。

## 不 emit 的那 159 個

原本以為這張票的 skip 清單會是共用 epilogue 那類反編譯碎片——前作 FD2 的 routing 有六個。查下來 `pool_fdps` 一個都沒有：514 支最小的也有 28 行反編譯碼，全是真的 function。`orphan_block` 標記的 30 支是沒有 caller 的死碼（多半是 CD API 沒用到的入口），但那是真程式碼，還是要 emit 才連得起來。

真正該 skip 的是另一批：Ghidra 必須給名字、但編譯器會自己產生一份的東西。222 個 `binary_artifact_*` 裡，被遊戲程式碼讀到的有 159 個，分四類——字串字面值 62、區域陣列初值 87、switch 跳躍表 7、浮點常數池 3。

寫分類規則時犯了一個錯：最後一條寫成 bare prefix `binary_artifact_` 當 catch-all，結果三個 `binary_artifact_fp_const_*` 被貼上 `<compiler:switch-table>` 的標籤。行為上都是 skip 所以測不出來，是列出清單肉眼看才發現的。改成四條具名 pattern，並加了一條檢查：遊戲程式碼讀到、卻既不是 `data_fdps_*` 也不符合任何 skip pattern 的符號，直接報成 problem。這條檢查一加就抓到 11 個 `switchdataD_*` 和 `default` ——那是反編譯器自己的 switch 標註，和跳躍表共用位址，不是要定義的符號，加進忽略名單。

catch-all 那個錯值得記：一個「剩下的都算某某類」的規則，錯的時候不會失敗，只會安靜地給錯理由。

## 順手改掉的兩份真相

`emit_state.json` 原本同時記進度和目標檔，`next_batch.py` 從它產生工作清單。現在落點的正本是 `routing.json`，兩邊都留一份 `target` 就是兩份會分歧的真相，而分歧的症狀是同一支 function 被寫進兩個檔、連結器不會抱怨。

改成 `next_batch.py` 以 `routing.json` 為名冊、`emit_state.json` 只出進度，兩邊 `target` 不一致時報 CONFLICT 並拒絕發工作清單。`emit_state.json` 的 `_doc` 也改了，它現在明說落點不在這裡決定。

跑 `--stats` 出來是 514 支：1 committed、513 pending。票 21 那支 `fdps_menu_find_first_enabled_entry` 當初暫定放 `menu.c`，這次規劃確認就是 `menu.c`，不必搬。
