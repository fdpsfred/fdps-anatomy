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

**Ghidra 的寫入集中在 Bookkeeper。** Emitter 與 reviewer 對 Ghidra 唯讀，發現描述錯誤時把修正寫進自己的判定檔，由 bookkeeper 在審查通過後一次套用，套用後跑 Ghidra 側的閘門並重新匯出文字快照。判定與落地分離的理由見 [ADR-0007](../docs/adr/0007-workflow-automation-and-agent-context.md)。

**判定寫檔，只回傳摘要。** 完整的判定寫進 `workspace/code_emit/verdicts/<addr>.emit.json` 與 `.review.json`，回傳給 workflow 的只有約 200 byte。「這一項做完了沒」由讀得到檔案的下一個角色回報，不採信寫檔者自己的宣稱。

## emit 的 gate

`python tools/build_gate/gate.py check --target emittest`。`emittest` 目標把 `src/` 的全部生產程式碼與 `tests/` 的全部測試編譯連結成一個 DOS/4G 映像並在 DOSBox-X 裡實際執行。通過的條件是**零錯誤、零未解析符號、沒有基準值未記錄過的警告、全部測試通過**。

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

**`__LINE__` 在 `CHECK_EQ` 裡不可用。** wcc386 10.0a 只有在巨集呼叫位於行首時給出正確的行號；跟在同一行其他 token 後面時給的是前處理後串流的行號，會落到檔案結尾之外。所以失敗訊息用「測試名稱＋該測試內的第幾個檢查」定位，不用行號。

## 工作狀態與續跑

`tools/code_emit/data/emit_state.json` 是進度的正本，進版控。一個 function 一筆，`status` 依 `pending → emitted → reviewed → committed` 推進，`failed` 與 `skip` 是終態且必須出現在收尾報告裡。

**「已完成」不能只看狀態欄。** 每筆記下 emit 當時所依據的 Ghidra body size，續跑時與 [`ghidra_snapshot/functions.txt`](../ghidra_snapshot/_index.md) 比對；function 後來變了大小，舊的 emit 描述的是已經不存在的程式碼，該筆退休並重回工作清單。

每一支通過的 function 是一個獨立的 commit，所以任何中斷最多只損失飛在半空的那一支。未通過的 function 由 workflow 清掉工作區的殘留並把狀態記成 `failed`——留著半成品的話，它會出現在下一支 function 的 diff 裡並被當成別人的改動 commit 掉。

## 檔案落點

每個 function 與資料符號的目標 `.c`／`.h` 由票 21.5 的 routing 規劃決定，結論屆時成為「哪個符號在哪個檔」的唯一正典。在那之前 `emit_state.json` 的 `target` 欄是暫定值。
