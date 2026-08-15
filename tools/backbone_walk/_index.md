# backbone_walk — 骨幹走查

把從進入點到主迴圈的路徑逐一走過並語意命名（票 12）。

| 檔案 | 用途 |
| --- | --- |
| `backbone_walk.js` | Workflow 腳本：每個 agent 只讀一個 function，回傳結構化判定 |

## 執行

```
Workflow({ scriptPath: "tools/backbone_walk/backbone_walk.js", args: [ ...一批 function... ] })
```

`args` 的每一筆是 `{addr, name, size, callers, callees}`，來源是 `workspace/call_graph/backbone_queue.json`。腳本對每筆各開一個 agent。

## 為什麼 agent 只讀不寫

兩個理由：

**逐一判定的結構性強制。** 工作清單只存在於腳本的迴圈裡，每次 `agent()` 呼叫的提示只帶一個位址。清單從來沒有被交給任何一個 agent，所以「拿到清單自己分配」這種退化在結構上不可能發生（[ADR-0002](../../docs/adr/0002-no-batch-processing-per-function.md)）。

**寫入不併發。** agent 回傳判定，改名與 plate comment 全部由 orchestrator 事後逐一套用。多個 agent 同時寫 Ghidra 會不會衝突是票 13 要驗的事，這裡不需要冒那個險，順帶讓每一筆判定在落地之前都被人看過一遍。

## agent 回傳的欄位

`proposed_name`、`pool`、`role`、`subsystem`、`plate_comment`、`params`、`cc_note`、`evidence`、`confidence`、`walk_next`、`open_question`。

`walk_next` 是 agent 認為值得繼續往下走的 callee，走查的下一批由它與呼叫圖共同決定，而不是機械地照深度展開。

`confidence` 為 `low` 的判定不直接落地，要另外處理——寧可留著 `FUN_*` 也不要一個看起來合理但錯的名字，因為錯的名字會被抄進知識庫與重建的 C 原始碼。

## 提示裡刻意寫死的幾件事

- **反組譯是第一手，decompiled C 只是第二意見。** 特別是簽章裡的 `unaff_EBX` 通常代表 Ghidra 猜錯，不是真的讀了 EBX。
- **不准提 PascalCase 名稱。** 工具會警告名字該是 PascalCase，那個警告對本專案是錯的，提示裡明講它是預期的，免得 agent 順從工具改名（見 [`tools/ghidra_config/_index.md`](../ghidra_config/_index.md)）。
- **不確定就說不確定。** 提示明確要求寧可回 `low` 加 `open_question`，也不要編一個用途把欄位填滿。

## 落地流程

1. `make_apply.py <batch.json> <decisions.json> <apply.json>`——把 agent 的判定與 orchestrator 的裁決合併。`decisions.json` 每筆帶 `_why` 記錄裁決理由，與提案不同時另外用 `plate_prefix` 在 plate comment 前面說明為什麼改。
2. `ApplyBackboneWalk.java`——寫入名稱、plate comment 與 no-return 旗標。這支腳本不做任何判斷，只是轉錄，存在的理由是長 plate comment 不該靠人手重打。
3. prototype 另外用 MCP 的 `set_function_prototype` 設，因為它會驗證簽章文字。**`__watcall` 不能寫在 prototype 字串裡**，會回 `Can't resolve return type`；要用獨立的 `calling_convention` 參數。`__cdecl` 寫在字串裡則可以。
4. 跑 `tools/ghidra_baseline/AuditGhidraBaseline.java` 確認 gate 仍是孤立程式碼 0、error bookmark 0。

## 標記 no-return 會產生孤立程式碼

把一個 function 標成 no-return，Ghidra 會砍掉它每個呼叫點的 fall-through 流程，呼叫點後面的指令因此可能掉出所屬 function 的 body。如果那些指令其實是別處跳進來的，它們就變成孤立程式碼，稽核的 gate 會亮。

實例：`crt_cmain`（`0x4df4c`）標成 no-return 之後，`crt_cstart_body` 的 body 從 538 byte 縮成 535，尾巴的 `0x43527`–`0x43529`（`POP EAX; JMP 0x4354d`）掉了出來。那三個 byte 是終止路徑的共用尾巴，由 `crt_exit` 內的 `0x42e3a` 跳進來，本來就屬於同一段組語。修法是把 body 範圍補回去，不是把 no-return 收回——那個 function 確實不返回。

**標記 no-return 之後一定要重跑一次稽核。**

命名規則的正典是 [`rebuild_info/naming.md`](../../rebuild_info/naming.md)。
