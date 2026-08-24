# logic_naming — 遊戲邏輯命名

把 `pool_fdps` 的每個 function 逐一讀過 assembly 之後給它語意名稱、語意參數名、確認過的 calling convention 與描述行為的 plate comment。這是票 15 專屬的 workflow，不是給別票用的框架（[ADR-0007](../../docs/adr/0007-workflow-automation-and-agent-context.md)）。

| 檔案 | 用途 |
| --- | --- |
| `naming_ticket15.js` | 主 workflow：判定 → 落地 → 重新匯出證據 → 兩道 gate → 撞名仲裁 → 回掃 → 收尾報告，全程無人介入 |
| `DumpNamingState.java` | 匯出每個 `pool_fdps` function 的 assembly、decompilation 與鄰居關係，唯讀 |
| `build_worklist.py` | 算出還沒命名的清單，並退休描述已不存在的 body 的判定檔 |
| `build_vocabulary.py` | 產生命名語彙頁：本程式已定案的名稱＋前作 FD2 的用字統計 |
| `merge_pitfalls.py` | 把判定的 `pitfall` 欄位補進 plate comment 的 `Rebuild note` 段（給早於該規則寫成的判定用，冪等） |
| `ApplyNamingVerdicts.java` | 把名稱、簽章（含參數名）、plate comment 轉錄進 Ghidra |
| `AuditNaming.java` | 本票專屬的 gate：預設命名殘留、參數殘留、前綴、撞名、convention、plate |

## 執行

```bash
python tools/logic_naming/build_worklist.py
```

從 `workspace/logic_naming/worklist.json` 取 `full`，放進 `args.addrs`：

```
Workflow({ scriptPath: "tools/logic_naming/naming_ticket15.js",
           args: { addrs: [...], roundSize: 16, maxFunctions: 120 } })
```

不給 `addrs` 時 workflow 自己開一個 agent 跑 `build_worklist.py` 並讀回清單——腳本本身讀不到檔案系統，這是唯一的取得途徑。**可重跑**：判定檔存在且 `covers.body_sha` 與現況相符的 function 不會再判一次，所以每次呼叫都從上次停下的地方接。416 個 function 塞不進一次 session，分次跑是常態。

## 工作清單是固定的，順序是葉子優先

票 12 的清單靠 BFS 一輪一輪發現，本票的清單在票 14.2 收工時就定了。剩下的自由度只有順序，而順序有實質影響：**一個什麼都不呼叫的 function 用自己的位元組就能解釋自己，一個呼叫二十個別人的 function 有一半的內容是那二十個在做什麼**。所以 `build_worklist.py` 以 callee 數排序，讓葉子先做，caller 讀到的鄰居盡量已經有名字。

## 證據每輪都要重新匯出

命名一個 function 最有用的線索，是它已經命名的鄰居叫什麼。這件事讓匯出檔在每一輪之後就過期：這一輪給出的 40 個名字，下一輪的 agent 如果讀的是開跑前的快照就完全看不到。

因此落地階段除了轉錄，還會重跑 `DumpNamingState.java`（約 20 秒）與 `build_vocabulary.py`。這是本票與票 14.2 結構上最大的差別——那張票的證據（位元組、FID 命中）不會因為別人的判定而改變，本票的會。

## gate 有兩道

基準稽核（`tools/ghidra_baseline/AuditGhidraBaseline.java`）：孤立程式碼 0、error bookmark 0、`.object1` 未定義 byte 0。

命名稽核（`AuditNaming.java`）分兩種計數，這是它能在票跑到一半時執行的原因：

- **pending** — 還掛著 Ghidra 自己產生的 `FUN_` 名稱，也就是還沒有人判定過。票跑完時歸零。
- **violation** — 已落地的判定弄錯的東西：命名了卻還留著 `param_N`、名稱前綴不合 [`naming.md`](../../rebuild_info/naming.md)、名稱裡還有位址、兩個位址搶同一個符號、命名了卻沒有 plate comment、convention 不是這個 binary 用的兩種之一。**任何時候都不接受**，當輪修不掉就停。

撞名的判斷用 symbol source 而不是字串：Ghidra 會把 thunk 目標的名字當成 thunk 自己的名字回報，兩個位址因此看起來搶同一個符號，實際上只有一個位址 claim 過它。

## 錯誤處理

[ADR-0007](../../docs/adr/0007-workflow-automation-and-agent-context.md) 第五條的實作對照：

| 狀況 | 行為 |
| --- | --- |
| 單一 agent 沒回傳或沒寫判定檔 | 重試一次，仍失敗記進未完成清單並繼續 |
| 一輪裡的 agent 全部沒回傳 | 判定為上游失效，立刻停止並列出未動過的項目，不送重試 |
| 兩個判定搶同一個名字 | 落地腳本原樣保留並回報，workflow 開仲裁 agent 改輸家的判定檔，重跑落地 |
| 判定回報邊界有問題 | 只回報不處理——邊界是票 14.2 的結論，命名這一趟不動它 |
| gate 不過 | 該輪修復，修不掉停止 |
| Ghidra 沒回應 | 落地 agent 回報 `ghidra_responding: false`，立刻停止 |
| 已停止 | 不寫知識庫、不產下游清單，但仍輸出收尾報告與未完成清單 |

## 已知的坑

**workflow 腳本不能有 CR。** 用 Python 在 Windows 上寫 `.js` 時預設會把 `\n` 轉成 `\r\n`，Workflow 工具會以「script contains control characters」拒絕整份腳本。以 binary 模式寫，或寫完轉成 LF。

**`__watcall` 與 `const` 不能寫在 prototype 字串裡**，`__watcall` 要走獨立的 `calling_convention` 參數，`const char *` 寫成 `char *`。症狀是簽章整份被拒而 function 留著 Ghidra 原本的猜測。（沿用票 12 的結論。）

**Ghidra 全 binary 標成 `__watcall` 是自動分析的預設值，不是事實。** 這個 binary 以 `wcc386 -4s` 建置，預設是堆疊慣例 `__cdecl`；`__watcall` 只有手寫組語那一族是真的。見 [`rebuild_info/build_flags.md`](../../rebuild_info/build_flags.md)。

**prototype 裡的 function 名稱會覆寫 function 的名稱。** `ApplyFunctionSignatureCmd` 拿字串裡的名字去改名，所以落地腳本在套用簽章之前會把 prototype 的名字換成名稱軸判定的那一個——名稱由名稱軸決定，簽章軸沒有發言權。

**plate comment 不能引用還沒落地的名稱。** 判定 agent 一次只看一個 function，很容易在註解裡寫下「呼叫 `fdps_xxx`」，而那個名字要好幾輪之後才存在。提示規定引用鄰居時用它**當下**的名稱，還是 `FUN_` 就用位址。

**「有未解問題」不等於「值得重讀」。** 判定檔把兩者分成 `open_question` 與 `needs_rescan` 兩個欄位，回掃只收後者。理由是量測出來的：417 個判定裡 288 個留有 open question，而真正會被鄰居的名字改變的只有 133 個——多數問題問的是某個全域的語意或某個值的單位，命名一百輪也答不出來，重讀只會把同一句話再寫一遍。

**單一 function 的重建陷阱寫進它自己的 plate comment。** 判定的 `pitfall` 欄位同時寫進 plate 尾端的 `Rebuild note:` 段，因為判定檔在 `workspace/` 不進版控，而 plate 隨 Ghidra 快照進版控，寫 C 的人看的也是 plate。只有跨 function 反覆出現的模式才進 [`rebuild_info/pitfalls.md`](../../rebuild_info/pitfalls.md)。
