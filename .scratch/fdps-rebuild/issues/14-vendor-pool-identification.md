# 14 — 未辨識程式碼建 function 與 vendor pool 判定

**What to build:** 每個 function 是不是遊戲本體，有了明確答案。CRT、AIL、以及編譯器產物這三個 pool 的成員被逐一判定出來，剩下的才是需要 emit 成 C 的遊戲邏輯。

判定的前置是**先把 function 集合補齊**。票 10 的基準盤點留下 485 個沒有任何 reference 的未反組譯區塊、共 39,652 byte，其中 32,556 byte 在程式庫區（`0x3c000` 之後）、7,096 byte 在遊戲邏輯區。抽樣顯示多數是完整的函式——對齊填充之後接 Watcom 的 `53 56 57 55 89 e5 81 ec` prologue——只是呼叫圖走不到，所以自動分析從來沒有碰過它們。這批程式碼不先建成 function，pool 判定就會漏掉八成的量，而且漏掉的都在最需要判 pool 的程式庫區。清單見 `program_info/memory_layout.md`，可用 `tools/ghidra_baseline/AuditGhidraBaseline.java` 重新產生。

這三個 pool 的判定不需要遊戲資料當依據——它們靠 function 本身的特徵、字串參照、以及與已知函式庫的比對就能確認，所以不必等攻略基準真值。

**Blocked by:** 13

**Status:** ready-for-agent

- [ ] 未辨識為程式碼的區域逐一判定是程式碼還是資料，一次處理一個；是程式碼的反組譯並建成 function，是資料的定義型別並記錄
- [ ] 建完之後重跑基準稽核，確認孤立程式碼與 error bookmark 仍是 0
- [ ] 每個 function 逐一讀過 assembly 後判定 pool 歸屬，不使用位址範圍圈定
- [ ] CRT pool 成員辨識出來並以函式庫比對佐證
- [ ] AIL pool 成員辨識出來，邊界明確
- [ ] 編譯器產物（不對應任何原始碼的 function）辨識出來
- [ ] 剩餘的遊戲本體 function 清單產出，作為 15 號票的工作清單
- [ ] 判定結果與依據記錄在 Ghidra 的標記與註解中
- [ ] Ghidra 快照匯出並 commit
