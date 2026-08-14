# 10 — Ghidra 基準盤點與修復

**What to build:** Ghidra 中的程式回到一個乾淨、可信的起點。目前有兩個待釐清的狀況：重新自動分析後 function 數從 811 掉到 719，那 92 個差額的去向不明；以及 12 個既有的錯誤標記。這兩件事不解決，後續「零殘留名、零錯誤標記」的完成判定就沒有意義。

**Blocked by:** None — can start immediately.

**Status:** done

- [x] 查明 92 個 function 差額的去向，逐一確認是誤判的資料被正確排除，還是真的程式碼被漏掉
- [x] 被漏掉的程式碼（若有）重新建立為 function
- [x] 12 個錯誤標記逐一查明原因並修復，修復後手動移除標記
- [x] 確認記憶體區塊的讀寫執行屬性，以及資料段與未初始化段的精確分界
- [x] 確認沒有未被辨識為程式碼的區域，或明確記錄哪些區域是資料
- [x] 基準狀態以 Ghidra 快照工具匯出並 commit

## 結果

- 錯誤標記實際是 10 個不是 12。全部源自 Ghidra 的 LE loader 在頁邊界放的 `LE_PAGE_*` 標記——標記位址用的是檔案偏移而非載入位址，落在指令中間又被反組譯，造成 13 處錯開的指令流。逐處修復後刪除 78 個標記，error bookmark 歸零。
- 差額不是 92 而是更大：有 190 個區塊、31,503 byte 的程式碼落在所有 function body 之外。成因是這些函式只被函式指標表指到、沒有直接 `CALL`，自動分析不會為它們建 function。程式碼沒有遺失——舊快照的 719 個 function 位址在最終狀態裡一個不缺。
- 逐區塊讀過 180 個孤立區塊的反組譯後建立 function，並修正三個誤判成 function 的 switch case 區塊、一處被誤判成程式碼的浮點常數表（`0x50d82`，反正切多項式係數）。function 數 719 → 1,042，孤立程式碼歸零。
- 記憶體區塊屬性以 LE object table 為準修正為 `r-x` / `rw-` / `rw-`；BSS 分界確認在 `0x64000`（object 2 只有 4 個 file-backed page）。
- 未反組譯的 43,143 byte 已分類：對齊填充 745、被參照的資料 2,746、無參照 39,652。無參照的部分是連結器帶進來但呼叫圖走不到的程式碼，逐一建 function 屬於 orphan code 工作（spec story 24），不在本票範圍。

結論寫在 [`program_info/memory_layout.md`](../../../program_info/memory_layout.md)，複查工具是 `tools/ghidra_baseline/AuditGhidraBaseline.java`。
