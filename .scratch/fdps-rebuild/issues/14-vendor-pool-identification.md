# 14 — Vendor pool 判定

**What to build:** 每個 function 是不是遊戲本體，有了明確答案。CRT、AIL、以及編譯器產物這三個 pool 的成員被逐一判定出來，剩下的才是需要 emit 成 C 的遊戲邏輯。

這三個 pool 的判定不需要遊戲資料當依據——它們靠 function 本身的特徵、字串參照、以及與已知函式庫的比對就能確認，所以不必等攻略基準真值。

**Blocked by:** 13

**Status:** ready-for-agent

- [ ] 每個 function 逐一讀過 assembly 後判定 pool 歸屬，不使用位址範圍圈定
- [ ] CRT pool 成員辨識出來並以函式庫比對佐證
- [ ] AIL pool 成員辨識出來，邊界明確
- [ ] 編譯器產物（不對應任何原始碼的 function）辨識出來
- [ ] 剩餘的遊戲本體 function 清單產出，作為 15 號票的工作清單
- [ ] 判定結果與依據記錄在 Ghidra 的標記與註解中
- [ ] Ghidra 快照匯出並 commit
