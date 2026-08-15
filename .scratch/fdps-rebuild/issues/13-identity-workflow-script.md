# 13 — 逐項工作的 workflow 骨架

**What to build:** 一套可重用的 workflow 骨架，讓後續每張需要逐一處理 function 或 data 的票都能全自動跑完、agent 的 context 不隨處理量成長、而且「一個 agent 一個項目」由結構強制。

這張票原本要從零建這套流程並小規模試跑。實際上票 12 為了走完骨幹已經把它整個建出來並在 100 個 function 的規模上驗證過了（`tools/backbone_walk/walk_ticket12.js`，106 個 agent、0 錯誤、三輪落地 gate 全綠）。規則寫成 [ADR-0007](../../../docs/adr/0007-workflow-automation-and-agent-context.md)。**本票剩下的是把它一般化，以及補上票 12 沒有處理到的兩件事。**

原驗收條件中「以 3 至 4 的並行度試跑，驗證多個 agent 同時寫入 Ghidra 不會衝突」已經失效：ADR-0007 決定判定階段的 agent 一律不寫入 Ghidra，落地集中在單一序列的轉錄階段，所以併發寫入的情境不會發生，不需要驗證。

**Blocked by:** 12

**Status:** ready-for-agent

- [ ] 把票 12 的 workflow 抽成骨架，讓新的逐項工作只需要提供：工作清單的來源、判定檔的欄位、落地的方式、以及該工作的 gate
- [ ] 骨架含 ADR-0007 的四個必要段：判定（寫檔、回傳摘要）、轉錄落地、gate、回掃
- [ ] 續跑機制一般化：已完成的項目由判定檔的存在判定，重跑會跳過
- [ ] **Ghidra 連線中斷有明確的停止訊號**，不會在斷線後讓後續 agent continue 空轉（票 12 未處理）
- [ ] **agent 回傳無效或缺漏判定檔時有重試機制**，目前是直接放棄該項目並記錄（票 12 未處理）
- [ ] 骨架的用法與已知的工具坑寫進 `tools/` 底下的 `_index.md`
