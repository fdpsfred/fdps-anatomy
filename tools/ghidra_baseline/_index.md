# ghidra_baseline — Ghidra 基準狀態稽核

複查 `FDPS.LE` 在 Ghidra 裡的分析狀態是否仍然乾淨。唯讀，不寫入 Ghidra 資料庫。

| 檔案 | 用途 |
| --- | --- |
| `AuditGhidraBaseline.java` | 一次跑完六項複查並輸出報告 |

複查項目：

1. 記憶體區塊的讀寫執行屬性
2. LE object table 解析出的權限與初始化／BSS 分界（權威來源，用來對照第 1 項）
3. 孤立程式碼——落在所有 function body 之外的指令
4. 程式碼 object 內未反組譯的 byte，分成對齊填充、被參照的資料、無參照三類
5. error bookmark
6. function 總數、仍是 `FUN_*` 預設名的數量、calling convention 與簽章來源分布

結論寫在 [`program_info/memory_layout.md`](../../program_info/memory_layout.md)。

## 執行

透過 Ghidra MCP 的 `run_ghidra_script` 帶絕對路徑執行；`FDPS.LE` 不是當前程式時腳本會直接拋例外停下。輸出寫到 `workspace/ghidra_baseline/baseline_audit.txt`，可用第一個參數改成別的目錄。

報告末尾的 Gate 段列出兩個門檻值——孤立程式碼範圍數與 error bookmark 數，兩者都必須是 0。
