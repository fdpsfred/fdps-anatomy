# 13 — 身分判定 workflow 腳本與小規模試跑

**What to build:** 一套能結構性強制「一次只處理一個 function」的自動化流程。這是 ADR-0002 的實作核心——工作清單只存在於腳本中，每次 agent 呼叫的提示只帶一個 function，讓批次處理在結構上不可能發生。

**Blocked by:** 12

**Status:** ready-for-agent

- [ ] 腳本以工作清單驅動迴圈，每次 agent 呼叫只帶一個 function 的位址與名稱
- [ ] agent 回傳結構化結果，欄位包含判定的 pool、calling convention、命名、以及判定依據
- [ ] 逐 function 的完成狀態記錄在狀態檔中，流程可中斷可續跑，重跑會跳過已完成的
- [ ] Ghidra 連線中斷有明確的停止訊號，不會在斷線後繼續空轉
- [ ] 以 3 至 4 的並行度小規模試跑，驗證多個 agent 同時寫入 Ghidra 不會衝突
- [ ] 試跑結果人工逐一複查，確認判定品質符合要求後才擴大規模
- [ ] 並行度的安全上限確認並記錄
