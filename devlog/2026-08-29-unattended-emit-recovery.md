# 2026-08-29 票 21.6：把「被殺掉」變成可承受的事

票 21.6 的三個缺口在票 21 只跑一支 function 的規模下都不會發作，所以這一票的工作有一半是把「規模放大之後才會出現的失敗」先想清楚，另一半是真的去殺一次來確認想的沒錯。

## 一、失敗路徑上的清理，本身就依賴一個已經死掉的東西

原本的設計是：一支 function 失敗時，workflow 開一個 agent 把 `src/`／`tests/` 的殘留清掉，狀態記成 `failed`。這條路徑要求 session 還活著。

而撞到 usage limit 時，session 正是不活著的那個東西。它不是丟一個 `agent()` 攔得到的例外，是把整個 session 就地砍斷——`catch` 不會跑，那個清理 agent 不會被開出來，半成品原封不動留在工作區。

後果比「損失那一支」嚴重：下一次呼叫的第一支 function 一開跑，reviewer 讀到的 diff 混著上一支的殘骸，bookkeeper 的 `git add src tests` 把它一起 commit 進去，而 commit 標題寫的是別人的名字。腳本對這件事原本的處置是印一行「check git status before the next run」，也就是把責任交給人——正是這條 pipeline 存在的目的的反面。

推論很短：**唯一保證在「session 已經死過一次」之後還執行得到的時機，是下一次開跑的第一件事。** 所以收拾從「失敗時做」搬到「開跑前做」，變成 workflow 的第一段（Recover），排在取工作清單之前——因為它剛放回工作清單的那個位址，必須出現在這一次要的清單裡，不是下一次。

## 二、足跡故意不 commit

第三個缺口是「沒有辦法在不看工作區的情況下知道剛才在做哪一支」。中斷時狀態檔對飛在半空的那一支仍寫著 `pending`，對續跑是對的，但沒有任何地方記下「上一次死在這一支」，而工作區的殘留這唯一線索正是要被清掉的東西。

解法是每支 function 開跑前寫一筆 `in_flight`（帶時間）。實作上有一個判斷卡了一下：這筆要不要 commit？

- **要 commit**：每支 function 多一個「開工」commit，git 歷史被灌一倍的雜訊，而且要多開一個 agent。
- **不 commit**：那它就是工作區裡一個界線外的髒東西，會被自己的 Recover 段判成界線外而停下來。

最後走的是第二條，配上把 `tools/code_emit/data/emit_state.json` 明確列進可清理的界線裡。這樣它反而變成一個很乾淨的性質：**落地 commit 會把它推到 `committed`，所以它永遠不會活過一支跑完的 function；開跑時還讀得到 `in_flight`，就是上一輪死在那一支。** 不需要額外的 commit，也不需要額外的 agent——寫進 emitter 的第 0 步就好。

`in_flight` 與 `interrupted` 都不是終態，`next_batch.py` 的 `TERMINAL` 只有 `committed` 與 `skip`，所以重發規則完全不用改，本來就是對的。

## 三、界線畫在路徑上，不畫在歸屬上

Recover 段能碰的只有 `src/`、`tests/`、`emit_state.json` 三個路徑。有兩個看起來很聰明但被否決的想法：

- **「先判斷髒東西是誰的，再決定要不要清」**——不行。歸屬正是中斷之後最不可靠的資訊，能拿來判斷歸屬的東西就是那筆 `in_flight`，而它可能根本還沒寫出來（死在第 0 步之前）。所以 `src/`／`tests/` 髒了卻沒有任何 `in_flight` 認領它，照樣丟，只是在報告裡講明「殘骸沒有名字」。
- **「界線外的東西也順手清一清」**——不行，而且這是整票最要守住的一條。`workspace/` 裡是中斷現場的判定檔，下一輪要讀；已經 commit 的東西是別人做完的工作。界線外髒了就停下來報告，猜它是什麼就是在刪別人的一個下午。

## 四、取消 workflow 自己的預算判斷

`minBudgetPerFn` 在每支開跑前檢查剩餘預算，不夠就把剩下的全部記成 `not_started_budget` 並停止。這與「跑完呼叫者指定的數量」直接衝突：呼叫者說 40 支，它可能跑 12 支就停，理由是一個呼叫者沒有要求它管的東西。

值得記的是**為什麼當初會覺得這個 guard 有道理**：它的註解寫著「stop on a function boundary rather than half way through one，因為中斷會留下半成品要下一輪清理」。也就是說，這個 guard 是在替一個更根本的缺陷（沒有復原機制）打補丁。上面那套 Recover 做完之後，它的前提消失了——中斷留下半成品這件事已經被處理掉，就沒有理由再為了避免它而提前收手。刪掉。

## 五、真的去殺一次，順便撞到兩件沒預期的事

票裡寫明這三件事只在「被殺過」之後才看得出來，所以不能只讀程式碼。跑法是：跑一批（limit 1）→ 背景輪詢 `git status`，一偵測到 `src/` 變髒就 `TaskStop` → 檢查現場 → 再呼叫下一批。

**第一次啟動就被自己的 Recover 段擋下來，而且擋對了。** 當時我手上還有一個沒 commit 的 `emit_ticket22.js` 修正（見下），Recover 段一看就報 `out_of_bounds`，什麼都沒動就停：

```
"working tree dirty outside this pipeline"
out_of_bounds_paths: [" M tools/code_emit/emit_ticket22.js"]
```

這條原本只打算靠讀程式碼相信它，結果第一次跑就自己驗證了，而且是在一個我沒設計的情境（髒的是 workflow 腳本自己）。

那個修正本身是個蠢錯但值得記：report 段的 prompt 是 template literal，我在裡面寫了 `` `recovered` `` 想強調欄位名，反引號直接把字串截斷。Workflow 工具當場拒收並指出行號，成本是零，但這類錯誤在 template literal 裡完全看不出來——**這個檔案裡凡是要強調欄位名的地方一律不能用反引號**。

**殺在半空中的現場完全符合設計。** 第二次跑，01:20 偵測到 `src/gamedata.h` 出現就 `TaskStop`，現場是：

```
 M tools/code_emit/data/emit_state.json     <- in_flight，未 commit
?? src/gamedata.h                           <- 殘骸
```

狀態檔裡 `000109f0` 是 `"status": "in_flight", "in_flight_since": "2026-08-29 01:20"`，沒有任何 commit 產生。這正是 usage limit 會留下的樣子——`catch` 完全沒有跑。

**下一批自己收乾淨了。** `553c258` 只動了 `emit_state.json` 一個檔（+8 行，那筆 `interrupted`），`src/gamedata.h` 被丟掉，`000109f0` 重回工作清單並在同一輪重新 emit、通過 review 與 gate、落地成 `b4136c4`。全程沒有人介入。

## 六、驗證時抓到的第四個問題：界線是雙向的

`b4136c4` 的檔案清單裡混著 `tools/build_gate/gate.py`（+76 行）。

追下去發現不是夾帶：那支 function 是第一個引用票 23 全域的，逼出了 gate 的一個潛伏 bug（`undefined` 判成第一次連結而不是第二次），agent 修得完全正確，理由也誠實寫進了它自己那篇 devlog。問題在**它跟 function 的 emit 進了同一個 commit**。

bookkeeper 的指示是 `git add src tests tools/code_emit/data ghidra_snapshot`，然後「確認 `git status --porcelain` 沒有留下任何 tracked 檔案」。`gate.py` 不在 add 清單裡，於是它為了讓後面那個檢查過關，把它一起 add 了進去。這個漏洞有兩個後果：

1. 一個沒被 review 也沒被 gate 過的改動，藏在一個寫著「reviewer 通過、build gate 通過」的 commit 標題底下，之後沒有人找得到它。
2. 更要命的是它跟本票直接衝突：如果那一刻被殺，下一輪的 Recover 段會看到界線外的髒路徑而停下來等人——**「全程不需要使用者介入」就這樣被一個看似無關的 gate 修正破功了**。

所以界線是雙向的：Recover 段只准清 `src/`／`tests/`，反過來 pipeline 自己也只准把這兩個地方弄髒。修法是三處同時講同一件事——RULES 裡加一條「要改 pipeline 以外的東西就當場單獨 commit 掉」，bookkeeper 明文禁止把界線外的東西掃進落地 commit（要就先單獨 commit，並在 `problems` 裡報告），reviewer 看到界線外的未 commit 改動就 block。兩個誠實的 commit，不要一個不誠實的。

這一條是真的跑過才會發現的：純讀程式碼時，`git add` 的路徑清單看起來就是界線本身，看不出「後面那個 clean-tree 檢查會誘使人把清單外的東西也 add 進來」。

## 七、Code review 抓出來的：整套界線只擋得住我剛好測到的那一種死法

上一節的修法送去 review，結果被抓出七項，其中三項是 HIGH，而且每一項都指向同一個毛病——**我只驗證了「殺在 emit 階段」這一種死法，而那恰好是唯一不會踩到問題的一種**。

**`git add -N` 讓 checkout + clean 清不掉東西。** reviewer 那一段為了讓新檔出現在 diff 裡會跑 `git add -N -- src tests`，而 intent-to-add 的檔案是**在 index 裡**的。實測（scratch repo）：

```
 A src/new.c          <- add -N 之後
git checkout -- src   <- 把它截成 0 byte，沒有移除
git clean -fdq src    <- 當它是 tracked，跳過
 A src/new.c          <- 還在，0 byte
```

也就是說：從 review 開始到落地為止——一支 function 大半的時間——被殺的話，Recover 段清不乾淨，`clean_now` 是 false，run 停下來等人。我的 kill 測試是在 `?? src/gamedata.h` 還是 untracked 的時候下手的，剛好在 reviewer 跑之前，所以完全沒踩到。`abandonPrompt` 有一模一樣的缺陷，而且更糟：driver 只看 `cleaned.committed`、不看工作區，於是那個 0 byte 的檔會被下一支 function commit 成它的。

修法是 pathspec 的 `git reset -- src tests` 排在 checkout 與 clean 之前（實測補上之後乾淨），driver 同時檢查 `tree_clean !== false`。這個坑值得記在心裡的形式是：**`git add -N` 的註解原本寫著「It stages nothing」，那句話是錯的**——它不 stage 內容，但它 stage 路徑，而清理指令在意的正是後者。註解已經改掉。

**每一批跑完都會留下未 commit 的 devlog，剛好卡死下一批。** Report 段寫 `devlog/<日期>-emit-<label>.md` 與 `devlog/runs/*.json` 但從來沒被要求 commit，而 `devlog/` 在界線外——所以下一批一開跑就 `out_of_bounds` 停機等人。這是本票的核心需求被自己的收尾段打破，而且**在我寫這篇的當下工作區裡就正躺著那兩個檔**。這一項最諷刺的地方是：它不是新引進的 bug，是原本就在的，只是在「界線」這個概念出現以前它不構成問題。加了界線之後，任何原本無害地留在工作區的東西都變成阻斷器——**引進一條不變式，等於把所有既有的違反從無害變成致命**。

**bookkeeper 自己就會把界線外弄髒。** 它第 2 步重新匯出 `ghidra_snapshot/`、第 4 步寫 `emit_issues.json`，而 commit 在第 5 步。中間被殺的話一樣停機等人。所以界線從「`src/`／`tests/` 兩個」擴成 pipeline 自己各段真正會寫的六個路徑——每一個都是可以丟掉重做的產出。同理，Split 段的職責就是改 `build_routing.py` 與 `code_layout.md`，我原本那條 RULES 等於禁止它做自己的工作，所以規則改成以「是不是你被指派的工作」為準，而不是以路徑清單為準。

`ghidra_snapshot/` 是六個裡面唯一不能單純還原的：中斷前可能已經改過 Ghidra 並存檔，還原成 HEAD 只會讓快照描述一個不存在的資料庫。那一項髒的時候改成重新匯出。

**stray 的 commit 順序會把整支 function 吞掉。** 我原本寫的是「先 `git add src tests …`，然後如果 `git status` 還有東西，先把它單獨 commit 掉」。順序反了：git commit 的是 index，不是意圖。agent 照直覺跑 `git add gate.py && git commit -m "..."` 會把已經 staged 的整支 function 一起 commit 在 gate.py 的標題底下，接著真正的落地 commit 因為「沒有東西可 commit」而失敗——正好是這段文字想防止的事情的反面。改成 stray 在 stage function **之前**處理，而且 commit 帶 pathspec：`git add <path> && git commit -- <path>`。

還有兩項比較小：bookkeeper 被要求在 `problems` 裡報告那個界外 commit，但 driver 只在失敗分支讀 `problems`，成功時整個丟掉——於是「這支 function 期間有一個界外 commit」這唯一的記錄消失，正是那段文字存在的理由；以及 reviewer 第 A 步同時寫著「知識庫改動也要一起 review」與「未 commit 的知識庫改動是 blocking finding」，十行之內自相矛盾，前者刪掉。

## 八、票 21 那支 workflow 沒有跟著改

`emit_ticket21.js` 留在原狀。它是票 21 那一次實際跑過的東西的紀錄，改它會讓紀錄與事實對不起來；而 ADR-0007 本來就說不抽共用骨架，每張票寫自己的 workflow。票 23 的 workflow 照 `rebuild_info/emit_pipeline.md` 的「中斷復原」與「批次大小是呼叫者的決定」兩節做，共用的是原則不是程式碼。

## 八、驗證腳本語法的便宜方法

改完 workflow 想確認語法，但這台機器上沒有 node，也沒有其他 JS runtime。試過 `node --check`、找 `bun`/`deno`，全部沒有。

最後用的方法是：把腳本複製一份到 scratchpad，在第一個 `phase()` 之前插一行 `if (true) { return { parsecheck: 'ok' } }`，然後用 Workflow 工具跑那份複本。工具會先完整 parse 整個檔案才開始執行，所以語法錯誤當場退回並附行號，而那個 early return 讓它花掉 0 個 agent、0 個 token。實測 14 毫秒。

有一個坑：複本要用**不帶 BOM 的 UTF-8** 寫出去。PowerShell 的 `Set-Content -Encoding UTF8` 會加 BOM，Workflow 的權限檢查會把它當成控制字元而拒收（訊息是 "script contains control characters that would be hidden in the approval dialog"，跟語法完全無關，很容易誤判）。用 `[System.IO.File]::WriteAllText` 配 `New-Object System.Text.UTF8Encoding($false)`。
