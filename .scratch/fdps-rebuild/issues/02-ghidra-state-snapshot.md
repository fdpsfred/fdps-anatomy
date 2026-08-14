# 02 — Ghidra 狀態文字快照工具

**What to build:** 執行一個指令就能把 Ghidra 目前的分析成果匯出成純文字，讓「Ghidra 改了什麼」變成 git 看得到的 diff。這是 ADR-0005 的實作。

**Blocked by:** None — can start immediately.

**Status:** ready-for-agent

- [ ] 匯出內容涵蓋 function 清單與簽章、calling convention、pool 標記、plate comment、struct 與 enum 定義、label、global 命名
- [ ] 輸出為穩定排序的純文字，同樣的 Ghidra 狀態重複匯出得到相同結果（diff 不會有無意義的雜訊）
- [ ] 所有檔案讀寫顯式指定 UTF-8 編碼
- [ ] 匯出結果進版控，並在 README 說明匯出時機與 commit 的關係
- [ ] 對目前未開工的狀態跑一次，產生基準快照
