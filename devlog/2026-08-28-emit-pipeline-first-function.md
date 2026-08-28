# 2026-08-28 emit 流程首跑：一個 function 走完全程

票 21 要的東西不是「emit 出一個 function」，是「把 emit 這條流水線從頭到尾跑通一次，而且跑的時候不用有人在旁邊看著」。今天這一跑就是那個驗收：工作清單裡放一個 function，腳本自己走完 emit → review → 修正迴圈 → build gate → commit，收尾回掃，然後把帳記回 `emit_state.json`。

結果是乾淨的。attempted 1、committed 1、skipped 0、unfinished 0，沒有停止訊號，修正迴圈一輪都沒進，gate 一次過，回掃段沒有東西可掃。commit 是 `3221228`。既然乾淨，就別把它寫成史詩——下面把時間花在兩件比較有價值的事上：這一跑實際證明了什麼，以及它**沒有**證明什麼。

## 為什麼是序列，為什麼是這個 function

序列不是保守，是被 reviewer 的視野逼出來的。reviewer 要看到「emitter 本輪到底改了什麼」，而它拿到的視野就是工作目錄本身的未提交狀態。這代表同一時間只能有一個 function 在飛：兩個 agent 同時寫 `src/`，reviewer 看到的 diff 就是兩個人的混合物，它再怎麼獨立驗證也分不出哪一行是誰的。要提高併行度得先解決這個（worktree 隔離之類），那是另一張票的事，今天不碰。

試跑對象挑 `fdps_menu_find_first_enabled_entry` @ `000160e0` 是刻意選的：純函式，唯一的輸入是一個指標參數，沒有全域、沒有 callee、沒有浮點、沒有 vendor 呼叫。挑它的理由寫在 `emit_state.json` 的 note 裡——**流程的第一次試跑不該同時相依票 23 的資料 emit**。如果第一個 function 引用了全域，那 emit 失敗時我分不出是流程壞了還是資料還沒備好，一個未知數就變兩個。代價是這個 function 太乖，乖到很多路徑沒被踩到（見最後一節）。

## emitter 這一輪做了什麼

emitter 從三源（plate comment、disassembly、decompiled C）產出 `src/menu.c`、`src/menu.h` 與 `tests/menu.c`，這是 `menu.c` 的第一個 function，所以三個檔都是從無到有。函式本體 26 行，編出來 77 byte。

calling convention 的判定是自己量的，沒有沿用 Ghidra 的標籤。prologue 連 push EBX/ESI/EDI/EBP 四個 callee-saved 才 `MOV EBP,ESP`，所以第一個堆疊參數落在 `[EBP+0x14]`——`0001610d` 的 `ADD EAX,[EBP+0x14]` 正好讀那格，入口沒有任何暫存器被讀。epilogue 是裸 `RET` 沒有立即數，唯一的呼叫端 `0002534b` 是 `PUSH EAX / CALL / ADD ESP,0x4`，caller 清。所以是 stack 傳參、caller cleans。Ghidra 標的 `__cdecl` 這次剛好對上，但判斷不是從那來的，這點 emitter 的 `cc_evidence` 有寫清楚。程式碼裡用 `#pragma aux ... "*" parm caller []` 明確宣告，兩份原始碼裡都 grep 不到 `__cdecl` 或 `__watcall`。

測試九個、斷言十三個，全部從 assembly 推出來而不是從 emit 出來的 C 推出來。比較有意思的兩個：

- `menu_any_nonzero_is_disabled`，descriptor 給 `{-1,7,0,0}` 期望 2。這條專門打「把 `CMP/JNZ` 讀成正負號判斷」這個誤讀——若照 sign test 寫，`-1` 會被當成可選，答案會變 0。這也是這個 function 唯一真正踩到的實機契約（契約 C）。
- `menu_bound_is_four`，給六個 int、唯一可選的放在 index 4，期望 -1。這條打「上限來自呼叫端或 sentinel」的誤讀，把 `JL 4` 是寫死的這件事釘住。

另外兩個測試的輸入是真的資料而不是編的：`00024d60` 那十六個 byte（全零）是呼叫端 `000252c8` 用 MOVSD 搬到 `[EBP-0x54]` 的那份 descriptor，以及 `00025340` 打上 patch（`MOV [EBP-0x50],1`）之後的狀態。

## reviewer 沒有偷懶的地方

reviewer 十項檢查全 pass，`blocking_issues` 空。值得記的是它**沒有**照單全收 emitter 的說法這幾處：

- 控制流是自己從 disassembly 重推的，不是比對 emitter 的敘述。
- build 數字是自己去讀 `BUILD.OUT` 與 `TEST.OUT`（`0 warnings, 0 errors`、`Code size: 77`、`total=13 failed=0 / verdict=ok`），不是相信 emit verdict 裡抄的那份。
- 兩個「真實資料」測試的出處自己去 image 讀了一遍——`00024d60` 起十六個零 byte、下一個 function 從 `00024d70` 開始，以及呼叫端的 patch 指令。這一步是有意義的：如果 emitter 編了一份假的遊戲資料當測試輸入，測試會漂亮地通過而且沒有任何機制會抓到。

它提了一個 Ghidra 修正（非阻擋）：`000160e0` 的 plate comment 還寫著 `FUN_000252b0` 與 `FUN_00016130`，這兩個 function 早就命名成 `fdps_battle_item_menu` 與 `fdps_menu_animate_open` 了，舊拼法現在搜不到東西。reviewer 自己不寫 Ghidra，這筆交給後面的階段套用，`ghidra_applied: 1`，快照的 `comments.txt` 有對應的 diff。

三條非阻擋的 note 記在這裡，因為它們現在不值得動、以後可能會咬人：

1. `src/menu.h` 的檔頭註解（繼承自 plate）提到 `fdps_battle_action_menu` 會自己填 descriptor。那句話講的是資料來源，但讀 header 的人可能誤以為有第二個呼叫端——`get_xrefs_to 000160e0` 只有一筆。等 `menu.c` 長出第二個 function 時要把那句話重新指向真正吃這個陣列的人。
2. 區域變數叫 `index`，會遮蔽某些 Watcom 標頭宣告的非標準 `index()`。今天 `menu.c` 只 include 了什麼都不 include 的 `menu.h`，零警告；哪天這個編譯單元 include 了 `<string.h>` 就會冒出來。
3. assembly 兩個出口共用一個 epilogue（走 `[EBP-0x4]` 這格結果暫存），C 寫成兩個 `return`。值一樣、不可觀測，故意不擋。

## 這一跑沒有證明的事

這才是三個月後回來看真正需要的段落。乾淨的首跑很容易被記成「流程 OK 了」，但實際被執行到的路徑只有 happy path：

- **修正迴圈沒跑過。** `rounds: 0`，reviewer 第一輪就通過。所以「reviewer 打回 → emitter 依審查意見改 → 重跑 gate」這條線今天一次都沒被執行，它還是紙上的。
- **gate 失敗路徑沒跑過。** 編譯零錯零警告、測試 13/13，gate 從來沒說過 NO，所以 gate 失敗後的修復迴圈同樣未驗證。
- **回掃段等於空轉。** `emit_issues.json` 是 `{}`，`issues: 0`，`rescan` 是空陣列。回掃的邏輯——「批次結束前重讀當時記下的等價性疑慮，看鄰近 function emit 完之後是否已經明朗」——今天沒有輸入可吃。
- **續跑與上游失效偵測沒跑過。** 沒有中斷、沒有 agent 沒回、沒有判定檔缺漏、沒有 `stopped`。`emit_state.json` 的 body_size 比對（Ghidra 那邊 function 改過大小就退休重 emit）也因為只有一輪而沒有機會作用。
- **這個 function 的難度不具代表性。** 八類實機契約裡七類的答案是「不適用」：沒有 CALL 所以沒有 vendor clobber list（A）、沒有全域所以沒有 tentative definition 相鄰性問題（B）、沒有計時（D）、沒有絕對位址（E）、沒有浮點（F）、frame 只有 8 byte 所以 stack probe 怎樣都無所謂（G）、沒有折進位移的常數索引（H）。真正會出事的 function 大部分卡在 B 和 E，而那兩條今天完全沒被壓力測試過。

換句話說：流程的骨架能動，但它的錯誤處理與難題處理仍然只是宣稱。票 22 開始跑量的時候，前幾個 function 應該刻意挑會引用全域、有 callee 的，讓 B 與 E 早點失敗——早失敗比晚失敗便宜。

## 收尾的 code review 抓到十二個東西，其中一個很難看

首跑乾淨之後對整條流水線跑了一次 code review。結論很清楚：**emit 出來的那支 function 是對的，出問題的全部在產生它的流程裡。** 這個分佈本身值得記——流水線第一次跑通的時候最容易只看產物、不看流水線。

最難看的一個：**reviewer 看不到新增的檔案。** 它拿到的指令是 `git --no-pager diff HEAD -- src tests`，而 `git diff` 不顯示未追蹤的檔。今天這一輪 `src/menu.c`、`src/menu.h`、`tests/menu.c` 三個檔全是新建的，所以 reviewer 那一步的輸出是空的——「看到 emitter 本輪的精確改動」這條要求，在最需要它成立的場合（一個模組的第一支 function）正好不成立，而且每個模組的第一支都會撞到。它今天沒有變成事故，是因為 checklist 後面幾步逼它自己去讀原始碼和 assembly，所以它實際上把該看的都看了；但那是靠別的步驟補起來的，不是這一步在運作。修法是先做一次 intent-to-add（`git add -N -- src tests`）再 diff，那不會真的 stage 任何東西。

第二個是設計上的：**判定為 skip 的 function 根本沒被寫回狀態檔。** 原本的分支在 skip 時刻意跳過記帳那一段，理由是「沒有東西要清理」——但記帳不只是清理，它是唯一會寫 `emit_state.json` 的地方。結果就是：反編譯器碎片被判定成不 emit，下一次跑工作清單又把同一個位址發出來，再燒掉一次完整的三源閱讀得到同一個結論，永遠。`next_batch.py` 那邊還老老實實把 `skip` 列在終態裡，等一個永遠不會被寫進去的值。

第三個是我自己在修帳的時候寫進去的錯：commit 雜湊記進 `emit_state.json` 之後 `git commit --amend` 補上去。amend 會重寫 commit 物件，所以那個雜湊指向的是被丟掉的舊物件，永遠不是真正落地的那個 commit。這條的修法不是修流程而是**把欄位拿掉**——雜湊沒辦法寫進產生它的 commit 裡，這件事沒有漂亮解；commit 標題本來就帶著位址，`git log --grep "@ <addr>"` 就找得到。

其餘九個，值得記的三個：

- **gate 紅燈的修復迴圈繞過了 reviewer。** 一旦 reviewer 說 approved，之後 gate 失敗就是 emitter ↔ gate 來回，改完直接 commit，而 commit 標題寫的是「reviewer 通過」。gate 失敗多半代表 emit 的 C 是錯的，那正是最該再審一次的時候。改成一個迴圈，兩種入口，出口只有「approved 且 gate 綠」。
- **清理失敗會被報成成功。** abandon agent 用的是裸 `agent()` 而不是 `runAgent()`，回傳 null 或 `committed: false` 都會印出「tree cleaned and state recorded」。接著下一支 function 的 bookkeeper `git add src tests`，把上一支的半成品掃進自己的 commit——正好是 abandon 這個階段存在的目的。
- **測試 harness 的 `sprintf` 沒有邊界。** `CHECK_EQ` 把整個運算式字串化，長度由寫測試的 agent 決定，而提示裡沒有任何長度規定；目標是 DOS/4G 映像，沒有 guard page，蓋掉的正好是緊鄰 buffer 的 `test_total` / `test_failed`。也就是說一個夠長的運算式可以把「測試失敗」蓋成「測試通過」——閘門唯一不能有的失效模式。改成有界複製之後實測了一次：160 字元截斷、計數器完好、失敗照樣被報出來。

`emittest` 的警告檢查也少了一半：只掃 `Warning!` 文字，沒有一起看 per-unit 摘要行的計數，而 `diagnostics()` 的註解本身就寫著為什麼兩邊都要看。

這一節的教訓比首跑本身重要：**流水線跑出乾淨的結果，不代表流水線的每個保證都在運作。** 今天那支 function 之所以是對的，一部分靠的是它太簡單、以及 reviewer 的 checklist 有冗餘。換一支難的 function，上面第一條和第五條都會直接變成事故。

## 帳

- commit `3221228`：`src/menu.c`、`src/menu.h`、`tests/menu.c`、Ghidra 快照的 comments.txt、`emit_state.json`、`emit_issues.json`。往後要找某支 function 的落地 commit 用 `git log --oneline --grep "@ <addr>"`，狀態檔裡不存雜湊。
- 判定檔留在 `workspace/code_emit/verdicts/000160e0.emit.json` 與 `000160e0.review.json`。
- 這一跑的機器回報：`devlog/runs/2026-08-28-emit-ticket21.json`。
- agent 輸出量 34k token 換一個 function。以 600 個以上的規模看，這個數字本身就是票 22 要盯的成本指標之一。
