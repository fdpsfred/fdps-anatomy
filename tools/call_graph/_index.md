# call_graph — call graph 建立與結構分析

把 `FDPS.LE` 的呼叫關係從 Ghidra 匯出成圖，並算出叢集結構。只讀，不寫入 Ghidra 資料庫。

這裡算的全是整體事實——邊、可達性、連通分量、fan-in 排名——不對任何單一 function 下身分判定，因此可以用腳本處理（見 [ADR-0002](../../docs/adr/0002-no-batch-processing-per-function.md)）。

| 檔案 | 用途 |
| --- | --- |
| `BuildCallGraph.java` | 從 Ghidra 匯出 function、直接呼叫邊、函式指標表與表分派的間接邊 |
| `analyze_graph.py` | 算可達性、孤島分量、共用 helper 排名、遞迴環，產出報告與骨幹走查的工作清單。可達性算兩種：只從 LE 進入點出發（骨幹走查的工作清單用這個），以及進入點加上「進入點存在函式指標表 slot 裡」的 function 一起出發（[`program_info/architecture.md`](../../program_info/architecture.md) 引用的數字；指向 function 中間的 slot 是 `switch` 標籤，不算起點；邊指進 function 本體的中間也算走到那個 function）。後者另按 Ghidra 快照的 `pool_*` 標籤分池計數 |

## 執行

```
run_ghidra_script  tools/call_graph/BuildCallGraph.java     # → workspace/call_graph/graph.json
python tools/call_graph/analyze_graph.py                    # → report.md / graph.dot / backbone_queue.json / islands.json
```

`BuildCallGraph.java` 透過 Ghidra MCP 的 `run_ghidra_script` 帶絕對路徑執行，`FDPS.LE` 不是當前程式時直接拋例外停下。兩支都吃可選的輸出目錄參數，預設 `workspace/call_graph/`。

## 間接邊怎麼來的

程式裡大量的 function 沒有任何直接 `CALL` 指向它，只能經由函式指標表抵達。腳本的解法分三步：

1. **掃出 run**——已初始化記憶體中，連續 4 個以上的 dword 都指進某個 function body 的區段。判準是「指進 body」而非「等於 entry point」：80x87 模擬器的 opcode 表指向的是單一大 function 內部的標籤，用 entry point 當判準會把 176 項的表在第 49 項截斷。
2. **切 view**——run 裡被 `CALL`／`JMP` 當成分派基底參照的 slot 就是一個 view 的起點。view 從自己的基底延伸到下一個 view 的基底，因此同一段 run 的各個 view 恰好把它切完，沒有 slot 落在任何 view 之外。這一步是必要的：`0x601c4` 起算的 110 個 slot 在記憶體裡連續，實際上是三張各自有分派者的表。
3. **產生邊**——每個 view 的分派者對該 view 的每個 slot 目標各連一條邊。

沒有任何 cross-reference 的 run 不產生邊，但會完整列進報告——它們的基底是執行期算出來的，歸不到呼叫端。

表本身的結論寫在 [`program_info/memory_layout.md`](../../program_info/memory_layout.md)。
