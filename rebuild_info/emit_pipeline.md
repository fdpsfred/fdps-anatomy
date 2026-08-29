# Emit pipeline — 一個 function 怎麼變成 C

**驗證對象**：把 Ghidra 的分析結果變成 `src/` 裡一支通過驗證的 C function 的整條流程。角色分工、emit 時必須在程式碼裡表達什麼、測試怎麼組織、工作狀態放在哪，以此檔為唯一正典。

旗標組由 [`build_flags.md`](build_flags.md) 擁有，閘門的判定規則由 [`build_gate.md`](build_gate.md) 擁有，符號命名由 [`naming.md`](naming.md) 擁有，本檔只引用。腳本在 [`tools/code_emit/`](../tools/code_emit/_index.md)。

## 五個角色，一次一個 function

| 角色 | 做什麼 | 不做什麼 |
| --- | --- | --- |
| Emitter | 讀三源（plate comment、disassembly、decompiled C），寫 `src/` 的 C 與 `tests/` 的測試，自己跑一次建置 | 不寫 Ghidra；不碰別的 function |
| Reviewer | 從 assembly 獨立驗證，並讀本輪的工作區 diff | 不信 emitter、不信 decompiled C；不寫 Ghidra、不改程式碼 |
| Gate | 跑 build gate 的 `emittest` 目標 | 不修任何東西——修了下一輪的 emitter 就不知道壞在哪 |
| Bookkeeper | 套用兩份判定檔裡提出的 Ghidra 修正、更新工作狀態、commit | 不下任何判斷 |
| Rescan | 該批結束前重讀所有未決的等價性疑慮，此時可引用鄰居的判定檔 | 不改程式碼；不替鄰居下判斷 |

**併行度是 1，而且這是設計不是限制。** Reviewer 判斷「本輪改了什麼」的依據是工作區相對 `HEAD` 的 diff，所以同時只能有一個 function 在飛；兩個的話彼此的改動會出現在對方的審查範圍裡。要提高併行度必須先解決 reviewer 的檢視範圍問題（例如 worktree 隔離），那是獨立的決定。

**Reviewer 的視野要含新增檔。** 一個模組的第一支 function 會把 `.c`、`.h`、測試三個檔全部新建出來，而 `git diff` 看不到未追蹤的檔——不先做一次 intent-to-add（`git add -N -- src tests`），reviewer 對「本輪改了什麼」的視野正好在最需要看的那份程式碼上是空的。

**Gate 紅燈要繞回 reviewer，不是只繞回 gate。** 建置閘門失敗多半代表 emit 出來的 C 是錯的，那正是最需要第二次審查的時候；修完直接重跑 gate 就 commit，等於用「編得過」取代「審查過」，而 commit 標題寫的是後者。

**Ghidra 的寫入集中在 Bookkeeper。** Emitter 與 reviewer 對 Ghidra 唯讀，發現描述錯誤時把修正寫進自己的判定檔，由 bookkeeper 在審查通過後一次套用，套用後跑 Ghidra 側的閘門並重新匯出文字快照。判定與落地分離的理由見 [ADR-0007](../docs/adr/0007-workflow-automation-and-agent-context.md)。

**判定寫檔，只回傳摘要。** 完整的判定寫進 `workspace/code_emit/verdicts/<addr>.emit.json` 與 `.review.json`，回傳給 workflow 的只有約 200 byte。「這一項做完了沒」由讀得到檔案的下一個角色回報，不採信寫檔者自己的宣稱。

**Rescan 自己 commit，而且要挑對名單。** 兩件事都是這一段最容易寫錯的地方：

- **它跑在落地 commit 之後**，所以它對 `emit_issues.json` 的更新是未 commit 的，而 `tools/code_emit/data/` 正是收拾段會還原的路徑之一——不自己 commit 掉，下一批一開跑整輪回掃的結論就沒了，而證據在 `workspace/` 底下不進版控，重建不回來。
- **名單不能只取本批落地的 function。** 一則疑慮之所以懸著，是因為它其實在問鄰居的契約，而那要等落地了鄰居的**那一批**才答得出來——記錄它的那一批恰好是最不可能答得出來的一批。只看本批等於每則疑慮只有一次機會、還用在最差的時機，之後檔案就變成唯寫。正確的名單是「仍 open，且鄰居剛落地」，從 `emit_issues.json` 與 call graph 挑。也不能取「全部仍 open 的」：那會無界成長，每批重問幾百個資訊量完全沒變的問題。

`emit_issues.json` 的每一則都要有 `status`（`open`／`resolved`）與 `from`（`emit`／`review`），否則用狀態篩選未決疑慮的東西會靜默漏掉它們。**一個結論只記一次**：emitter 與 reviewer 記到同一件事時，settle emitter 那則，reviewer 那則用 `same_as` 指過去——兩份逐字複本的意思是將來發現其中一份錯了，只會改到一份，留下另一份繼續矛盾。

## 順序是 callee 先於 caller

工作清單不照位址排，照 call graph 的拓樸序排：一支 function 的 callee 全部先 emit 完，才輪到它。順序由 `tools/code_emit/emit_order.py` 從 call graph 算出來，`next_batch.py` 照著發。

理由是測試的可信度。還沒 emit 的 callee 在連結時被填成回傳 0 的 stub（見下節），此時替 caller 寫的測試量到的是 stub 而不是真的 callee；照位址排會讓幾乎每支 function 都處在這個狀態。實測 514 支排完只剩 **11 對** caller 早於 callee，全部來自唯一一個 10 支 function 的環——環沒有 callee-first 的排法，這是定義使然，不是排序沒排好。

環裡的成員與那 11 對會被明白告訴 emitter：哪些 callee 現在還是 stub，測試就不准斷言依賴它們回傳值的東西。

## 資料還沒 emit 之前怎麼連結

**編譯只要宣告，連結才要定義。** 全域資料的真值是票 23 的產物，但票 22 每一輪都要連結得起來，所以建置**連結兩次**：

| | 帶什麼 | 產出 |
| --- | --- | --- |
| 第一次 | 只有 `src/` 與 `tests/` 的物件 | 未定義符號清單 → `workspace/code_emit/undefined.json` |
| 第二次 | 加上自動產生的零填充 stub 模組 | 必須零未解析符號 |

第一次報出來的未定義符號**是預期產物，不是失敗**——它就是「已 emit 的程式碼要、而還沒有人定義」的完整集合，也就是**票 23 的權威工作清單**。它每次建置重新產生，所以會隨進度自己縮短，而且永遠不可能與程式碼不一致。

stub 模組由 `tools/code_emit/gen_stubs.py` 產生：資料照票 17 的型別與大小宣告（型別只為了對齊，內容一律是零），還沒 emit 的 function 給一支回傳 0 的空殼。**它不進 `src/`**，每次建置從頭產生、落在暫存區——stub 是「還沒有人下判斷」，放進 `src/` 就與真的 emit 出來的定義分不開了。

三類符號 `gen_stubs.py` 拒絕 stub，直接讓建置失敗：routing 不認得的名字（拼錯，或連結指令列漏了程式庫）、routing 標記為不 emit 的符號（字串字面值、區域陣列初值、switch 表——它們該在使用它的 function 裡面）、以及 `src/` 已經定義過的符號。

**stub 看不見的那一面：** 一個拼對了但拿錯的全域名字會被照樣 stub 成零，而零看起來很像一個合理的答案。這一類只有 reviewer 從 assembly 讀得出來，閘門讀不出來。

## emit 的 gate

`python tools/build_gate/gate.py check --target emittest`。`emittest` 目標把 `src/` 的全部生產程式碼與 `tests/` 的全部測試編譯連結成一個 DOS/4G 映像並在 DOSBox-X 裡實際執行。通過的條件是**零錯誤、零未解析符號、沒有基準值未記錄過的警告、全部測試通過**。「零未解析符號」判的是第二次連結，第一次的那份是清單不是錯誤。

這個目標**不做映像等價比對**。它的內容按設計每 emit 一支 function 就變一次，拿它比對基準值只會每次都紅、每次都被推進，那是一個被訓練成永遠說 yes 的閘門。判定欄位因此顯示 `not compared`，理由與其他目標的差別見 [`build_gate.md`](build_gate.md)。

emitter 自己在寫的當下跑的是 `python tools/code_emit/build_emit.py all`，同一套建置，少了閘門的自我測試套件。兩者的建置流程是同一份實作。

## calling convention 要在程式碼裡宣告

每一支 emit 出來的 function 都要在它的原型旁邊寫出自己的 calling convention，不靠 `-4s` 帶過：

```c
#pragma aux fdps_menu_find_first_enabled_entry "*" parm caller [];
```

`parm caller []` 就是堆疊慣例——引數全在堆疊、呼叫端清理，也就是 `-4s` 的預設。**實測這條 pragma 與不寫它產生逐 byte 相同的機械碼**（同一個 translation unit 加與不加各編一次，`.obj` 只差三個 byte，全部落在 Watcom 記錄來源檔時間戳的 COMENT 記錄裡，控制組重編一次也差同樣那三個）。所以它是純粹的宣告，不影響產出。

`"*"` 這一段是必要的：它把符號名固定成不加裝飾。**不能改用 `__cdecl` 關鍵字**，理由見 [`pitfalls.md`](pitfalls.md)。

## 八類隱性契約

前作 FD2 在實機階段修掉的八類 bug，共同點是**編譯全綠、單元測試全綠都驗不出來**——它們不是 function 算錯，而是 function 與環境之間的某個隱性契約與原版不一致。每一支 emit 的 function 都要逐條對過，不適用的也要說明為什麼不適用（[ADR-0003](../docs/adr/0003-manual-playtest-over-automated-golden.md)）。

| 類 | 隱性契約 | 檢查什麼 |
| --- | --- | --- |
| A | vendor function 真正破壞哪些 caller-saved register | `modify` 清單是精確集合語意。堆疊慣例下 EBX 是 callee-saved，只列一部分等於把污染搬家。四個 `eax ebx ecx edx` 要嘛全列要嘛不列 |
| B | 未初始化全域的擺放順序與相鄰關係 | Watcom 對 tentative 定義的順序與相鄰不保證，實測甚至反序。任何把相鄰全域當單一陣列索引的地方，必須 emit 成真的陣列或 struct |
| C | 全域與欄位的號性 | 號性是行為不是表示法，一進入比較就分歧。看 assembly 的 `JGE`／`JLE` 對 `JAE`／`JB` |
| D | 時序敏感熱迴圈的指令數 | 音效時長是實時的、繪圖時長隨模擬器 cycles 縮放，「等價而更短」的寫法會破壞原版依賴的時序平衡 |
| E | 硬編的絕對位址 | 重建版不會把任何東西擺回原位，寫死的位址指向的是別的東西。一律改用符號 |
| F | math intrinsic 的呼叫形式 | sqrt／sin／cos 走 intrinsic 還是走具名 CRT function 屬於 codegen，標頭檔的 pragma 是其中一部分。弄錯會把原版從未執行過的 vendor 程式碼帶進 runtime |
| G | 哪些 translation unit 帶 `__CHK` | 全開會被中斷的私有堆疊誤殺，全關失去溢位防護。原版的遊戲程式碼是 `-s`（[`build_flags.md`](build_flags.md)） |
| H | 折疊後的基底歸給哪個符號 | 編譯器把常數索引折進位移後，Ghidra 會把基底歸給**前一個**符號。判準是「還原出的索引最大值超出宣告的元素數」。**build gate 完全看不到這一類** |

B、E、H 三類的共同症狀是「數值或指標讀到不相干的東西」。遇到這個症狀先問：這個讀取在原版是不是靠映像佈局才成立的。

## 測試怎麼組織

測試碼在獨立的 translation unit，連結生產程式碼但不修改它；`src/` 底下不存在任何條件編譯的測試 hook（[ADR-0003](../docs/adr/0003-manual-playtest-over-automated-golden.md)）。

| 規則 | 內容 |
| --- | --- |
| 一對一鏡像 | `tests/<stem>.c` 對應 `src/<stem>.c` |
| 註冊 | 該檔定義 `void run_<stem>_tests(void)`，用 `RUN_TEST` 登記每個 case。進入點由建置腳本從這個函式名產生，新增測試檔不需要接任何線 |
| 斷言 | `CHECK_EQ`，見 `tests/testharn.h` |
| 涵蓋政策 | 風險導向：數值計算、分支結構、狀態轉移，以及任何用到「CALL 之後的回傳值」的地方必須測；純繪圖副作用可延後並註明 |
| 期望值來源 | 攻略站數值 > Ghidra emulator 對純計算取得的 ground truth > 從 assembly 手推。**禁止拿 emit 出來的 C 自己的行為當期望值**——那種測試只證明程式碼等於它自己 |
| 真實遊戲檔 | 要讀遊戲檔的測試必須讀真檔：把 8.3 檔名列進 `tests/gamefile.lst`，建置腳本會把它暫存到執行目錄。捏造結構假檔證明不了任何事 |
| 不准斷言 stub | 全域資料在票 23 之前一律是零、還沒 emit 的 callee 一律回傳 0。**任何期望值取決於這兩者的斷言都不成立**——它今天會過，等真值落地那天變紅，而那是最糟的發現時機。行為由靜態表決定的 function 在測試內自備局部 fixture 表，不得為了測試提前 emit 該符號的真值 |

`src/fdpstype.h` 的 23 個遊戲 struct 由 `tests/fdpstype.c` 逐欄檢查：每個 struct 的 `sizeof` 與每個欄位的 `offsetof` 都對照 `ghidra_snapshot/data_types.txt` 記的偏移。期望值來自快照而不是標頭，所以它證明的是「編出來的佈局等於原版的佈局」，不是「標頭等於它自己」。兩個檔都是 `tools/code_emit/gen_types.py` 的產生物，不手改。

**`__LINE__` 在 `CHECK_EQ` 裡不可用。** wcc386 10.0a 只有在巨集呼叫位於行首時給出正確的行號；跟在同一行其他 token 後面時給的是前處理後串流的行號，會落到檔案結尾之外。所以失敗訊息用「測試名稱＋該測試內的第幾個檢查」定位，不用行號。

## 工作狀態與續跑

`tools/code_emit/data/emit_state.json` 是進度的正本，進版控。一個 function 一筆，`status` 依 `pending → in_flight → emitted → reviewed → committed` 推進，`failed` 與 `skip` 是終態且必須出現在收尾報告裡。只有 `committed` 與 `skip` 會讓 `next_batch.py` 把該位址從工作清單移除；其餘一律重發。

**「已完成」不能只看狀態欄。** 每筆記下 emit 當時所依據的 Ghidra body size，續跑時與 [`ghidra_snapshot/functions.txt`](../ghidra_snapshot/_index.md) 比對；function 後來變了大小，舊的 emit 描述的是已經不存在的程式碼，該筆退休並重回工作清單。**沒有記下依據的那一筆也算過期**——「沒人寫下這是照哪一份程式碼做的」不是「程式碼沒變」的證據，當成證據就會讓那筆永遠被跳過。

**判定為不 emit 的 function 也要寫回狀態檔。** 反編譯器碎片、連結器產物之類的東西是一個結論，不是一次失敗；不記下來的話，下一次跑工作清單又會把同一個位址發出去，而且每次都要燒掉一次完整的三源閱讀才能再得到同一個結論。

**狀態檔裡沒有 commit 雜湊。** 雜湊沒辦法寫進產生它的那個 commit 裡，事後 `--amend` 補上去只會得到一個指向被丟棄物件的雜湊。落地 commit 的標題帶著位址，用 `git log --oneline --grep "@ <addr>"` 找。

每一支通過的 function 是一個獨立的 commit，所以任何中斷最多只損失飛在半空的那一支。未通過的 function 由 workflow 清掉工作區的殘留並把狀態記成 `failed`——留著半成品的話，它會出現在下一支 function 的 diff 裡並被當成別人的改動 commit 掉。

## 中斷復原：開跑前收拾，不是失敗時才收拾

「最多只損失飛在半空的那一支」是對**下一次**的承諾，而它只有在 workflow 第一段先收拾工作區時才成立。撞到 usage limit 不會丟出任何腳本攔得到的例外，它是把 session 就地殺掉：失敗路徑上那個清工作區的 agent 從來沒有機會執行，半成品原封不動留在 `src/` 與 `tests/` 裡。下一支 function 的 reviewer 看到的 diff 混著前一支的殘骸，bookkeeper 的 `git add src tests` 把它一起 commit，而 commit 標題寫的是別人的名字——損失的不是一支，是兩支。

**唯一保證在「session 已經死過一次」之後還執行得到的時機，是下一次開跑的第一件事**，所以收拾放在那裡：取工作清單之前先看工作區，丟掉殘骸，把對應的 function 記成 `interrupted` 送回工作清單，然後才開始。

**能碰的東西由路徑決定，且不容協商。** 界線就是這條 pipeline 自己的各段會寫的那六個路徑，每一個都可以丟掉重做：

```
src/  tests/  tools/code_emit/data/  ghidra_snapshot/
tools/code_emit/build_routing.py  rebuild_info/code_layout.md
```

`workspace/` 不能碰——判定檔是中斷現場的紀錄，重跑要讀它；已經 commit 的東西不能碰，不 revert、不 amend、不移動 HEAD；界線外的路徑髒了就**停下來報告**，那不是這條 pipeline 的殘骸，猜它是什麼就是在刪別人的一個下午。`src/`／`tests/` 髒了卻沒有任何 `in_flight` 認領它，照樣丟——界線是路徑不是歸屬——但要在報告裡講明殘骸沒有名字。

`ghidra_snapshot/` 是唯一不能單純還原的一個：中斷前 bookkeeper 可能已經改了 Ghidra 並存檔，把文字快照還原成 HEAD 只會讓它描述一個不存在的資料庫。所以那一項髒的時候是**重新匯出**而不是還原，匯出來什麼就是什麼。

**清理要先 `git reset -- <路徑>`。** reviewer 為了讓新檔出現在 diff 裡會跑 `git add -N -- src tests`，而 intent-to-add 的檔案在 index 裡：`git checkout --` 只會把它截成 0 byte 而不是移除，`git clean -fd` 又把它當 tracked 而跳過，結果留下一個 0 byte 的 `.c` 被下一支 function 的 bookkeeper commit 成自己的。三個指令要湊齊：pathspec 的 `reset`（只 unstage，不動 HEAD）、`checkout`、`clean`。

**足跡故意不 commit。** 每支 function 的 emitter 動任何東西之前先把該筆寫成 `in_flight` 並帶上時間，這一筆留在工作區不落 commit：落地 commit 會把它推到 `committed`，所以它永遠不會活過一支跑完的 function；反過來說，開跑時還讀得到 `in_flight` 就代表上一輪死在那一支。這是「工作區髒」之外唯一的線索，而工作區正是要被清掉的東西。`in_flight` 與 `interrupted` 都不是終態，重發規則與 `pending` 完全相同——那一支從頭到尾沒有被判定過任何事，沒有東西需要保留。

**界線是雙向的：pipeline 自己也只准把那六個路徑弄髒。** 收拾段靠路徑分辨殘骸與工作，所以任何一段做出界線外的改動——建置腳本、gate、其他知識庫頁、devlog——都必須**當場單獨 commit 掉**，不能留在工作區，也不能讓它搭上 function 的落地 commit。留著會讓下一輪的收拾段判定成界線外而停下來等人，正好是這條 pipeline 存在的目的的反面；搭順風車則是把一個沒被 review 也沒被 gate 過的改動塞進一個寫著「reviewer 通過、build gate 通過」的 commit 裡，之後沒有人找得到它。兩個誠實的 commit，不要一個不誠實的。

這條對收尾那一段同樣成立：**批次結束寫的 devlog 與 run report 要自己 commit 掉**，否則一批跑完就留下界線外的髒路徑，下一批一開跑就停。

實作上有兩個順序陷阱：

- **界線外的東西要在 `git add src tests …` 之前先處理掉，而且 commit 要帶 pathspec。** git commit 的是 index 不是你的意圖：function 已經 staged 之後再 `git add <那個檔> && git commit`，會把整支 function 一起 commit 在那個檔的標題底下，然後真正的落地 commit 因為「沒有東西可 commit」而失敗。寫成 `git add <path> && git commit -- <path>`，並且擺在 stage function 之前。
- **判定「清乾淨了」要同時看 commit 與工作區。** 只確認狀態檔 commit 成功、不看工作區是否還髒，等於沒清——殘留會被下一支 function 的 `git add src tests` commit 成它的。

**收尾報告要分得出兩種沒落地。** 「還沒輪到」是 run 在它之前就停了，什麼都沒動、沒有東西要清；「跑到一半被中斷」是工作區裡有東西，要靠下一輪的收拾段處理。合成一個數字就是讓後者被當成前者，於是沒有人去看工作區。

## 批次大小是呼叫者的決定

workflow 不看自己的預算。它跑完呼叫者給的清單為止，停下來的理由只有三種：清單跑完、上游失效（[ADR-0007](../docs/adr/0007-workflow-automation-and-agent-context.md) 5.2／5.5）、外力中斷。

理由是職責：呼叫者說要跑 40 支，workflow 依一個沒人要求它套用的門檻在第 12 支收手，是在回答沒有人問的問題。預算耗盡的正確表現是被外部殺掉，而上面那段收拾機制就是讓「被殺掉」變成可承受的東西。一批跑完由呼叫者檢查結果、修掉問題、直接呼叫下一批，全程不需要使用者介入。

票 23 的 data emit workflow 自己寫，但照這一節做——[ADR-0007](../docs/adr/0007-workflow-automation-and-agent-context.md) 說不抽共用骨架，共用的是原則。

## 檔案落點

每個 function 與資料符號的目標 `.c`／`.h` 由 [`code_layout.md`](code_layout.md) 擁有，逐項對照表是 `tools/code_emit/data/routing.json`。emit 期間不做落點判斷：工作清單本身就帶著目標檔。

`emit_state.json` 只記進度，不記落點；兩邊對某支 function 的目標檔不一致時 `next_batch.py` 報錯而不是二選一。
