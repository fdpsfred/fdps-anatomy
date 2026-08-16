# backbone_walk — 骨幹走查

把從進入點到主迴圈的路徑逐一走過、命名、落地 Ghidra，並產出票 12 的知識庫頁面。

**這是票 12 專屬的 workflow，不是給別票用的框架。** [ADR-0007](../../docs/adr/0007-workflow-automation-and-agent-context.md) 明確決定不抽共用骨架——各票的工作形狀差異太大。這支腳本的價值是當範例：讀它可以看到那五條原則實際長什麼樣。裡面的小工具（`collect_verdicts.py`、`ApplyBackboneWalk.java`）可以照抄或改寫，但別把整支腳本當成新工作的起點去改造。

| 檔案 | 用途 |
| --- | --- |
| `walk_ticket12.js` | 主 workflow：BFS 走查、命名仲裁、逐輪落地、打標、寫知識庫與 devlog，全程無人介入 |
| `backbone_walk.js` | 只跑一輪讀取、不落地的簡化版，用於單獨檢視某一批 function |
| `collect_verdicts.py` | 把每個 function 的判定檔收攏成落地清單、prototype 清單與精簡索引 |
| `ApplyBackboneWalk.java` | 把名稱、plate comment、no-return 旗標轉錄進 Ghidra |
| `TagBackbone.java` | 打上 pool、subsystem、shared_helper 的 function tag |

## 執行

```
Workflow({ scriptPath: "tools/backbone_walk/walk_ticket12.js",
           args: { seeds: [...], alreadyDone: [...], maxFunctions: 100, maxRounds: 8 } })
```

`seeds` 是起走的位址，之後每一輪的工作清單由上一輪 agent 回報的 `walk_next` 決定——走查是被發現出來的，不是排定的。`alreadyDone` 讓重跑會跳過已完成的部分。

## 兩個撐住整個設計的性質

**一次一個 function。** 工作清單只存在於腳本的迴圈裡，每次 `agent()` 呼叫只帶一個位址，清單從頭到尾沒有交給任何一個 agent（[ADR-0002](../../docs/adr/0002-no-batch-processing-per-function.md)）。

**每一段的 context 都有界。** reader agent 把完整判定——plate comment、證據、prototype——寫進 `workspace/backbone_walk/verdicts/<addr>.json`，只回傳約 200 byte 的摘要。所以不管走了幾百個 function，workflow 腳本與後續每一段的 context 都不隨之成長。寫知識庫的那一段讀的是 `index.json` 這份精簡索引，不是一百份 plate comment。

## 各階段

| 階段 | 做什麼 | 一次處理幾個 |
| --- | --- | --- |
| Walk | 讀一個 function，判定身分並寫判定檔 | 1 |
| Arbitrate | 兩個 function 撞名時決定其中一個要改成什麼 | 1 |
| Apply | 把該輪的判定轉錄進 Ghidra 並跑稽核 gate | 一輪 |
| Rescan | 重讀一個尚未定案的判定，這次可引用鄰居的判定檔 | 1 |
| Tag | 打 pool / subsystem / shared_helper 標籤 | 全部 |
| Document | 寫 `program_info/architecture.md` 與 devlog | — |

Apply 與 Tag 是轉錄，不是判定：每個名稱、plate comment、prototype 都是某個只看過那一個 function 的 agent 判定的，這兩段只負責把它們無損地放進 Ghidra。

## agent 不寫 Ghidra，寫入集中在 Apply

reader 完全不碰 Ghidra 的寫入端。這樣一來多 agent 併發寫入的風險根本不存在，不需要另外驗證它安不安全，而且每輪的落地是單一序列動作，出事時範圍明確。

## 已知的坑

**`__watcall` 不能寫在 prototype 字串裡。** `set_function_prototype` 會回 `Can't resolve return type`，要用獨立的 `calling_convention` 參數。`__cdecl` 寫在字串裡則沒問題，所以症狀是一半成功一半失敗，容易誤判成型別問題。判定檔因此把 convention 獨立成一個欄位。

**`const` 也過不了。** 同一個 parser 對 `const char *` 回 `Can't resolve datatype`，寫 `char *` 即可——儲存方式相同，只是少了限定詞。

**calling convention 的預設是 `__cdecl` 不是 `__watcall`。** 這個 binary 以 `wcc386 -4s` 建置，用的是堆疊慣例，引數從 `[ebp+0x14]` 起、呼叫端清理（見 [`rebuild_info/build_flags.md`](../../rebuild_info/build_flags.md)）。Ghidra 全 binary 標成 `__watcall` 是自動分析的預設值，不是事實。零參數的 function 兩者無法區分，一律取 `__cdecl` 與 binary 的預設對齊。

**標記 no-return 會產生孤立程式碼。** Ghidra 會砍掉該 function 每個呼叫點的 fall-through，呼叫點後面的指令可能因此掉出所屬 function 的 body；如果那些指令其實是別處跳進來的，它們就變成孤立程式碼。實例是 `__CMain`（當時叫 `crt_cmain`）標成 no-return 後，`L$1_cstrt386_start_body` 的 body 從 538 縮成 535，尾巴的 `0x43527`–`0x43529` 掉了出來——那三個 byte 由 `exit` 跳進來，屬於同一段組語。修法是把 body 範圍補回去，不是收回 no-return。Apply 階段每輪都跑稽核 gate 就是為了當場抓到這件事。

## Rescan：為什麼需要它

reader 永遠只看自己那一個 function。如果某個 function 的身分完全取決於它的 callee，而那個 callee 要到後面幾輪才被走到，第一輪就只能回低信心——即使推翻它所需的證據稍後就會躺在同一個資料夾的另一個檔案裡。

實例是 `0x305a0`：body 只有一個 `CALL 0x3d8b2`，第一輪只能給佔位名稱；`0x3d8b2` 在第二輪被判定為 `AIL_shutdown`，身分就此明朗。第一次跑的時候沒有回流機制，是人在複查時補的。

Rescan 段就是那條回流：走查結束後把所有信心為 low 或 `open_question` 非空的判定挑出來，各開一個 agent 重讀，這次允許它讀鄰居的**判定檔**當證據。讀鄰居的判定是引用別人已經下好的判斷，不是替鄰居下判斷，所以「一個 agent 一個 function」沒有被打破。

重讀後如果仍然無解，agent 必須原樣保留並回報還缺什麼——提示裡明講「第二次嘗試不是硬掰結論的理由」。最多跑 `maxRescanPasses` 輪（預設 2），某一輪一個都解不掉就提早停，剩下的列進最終報告。

命名規則的正典是 [`rebuild_info/naming.md`](../../rebuild_info/naming.md)。
