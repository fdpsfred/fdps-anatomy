# 16 — CRT pool 版本判定補強

**What to build:** 編譯器小版本的結論不再建立在最弱的一環上。目前判定為 Watcom 10.0a，但排除 10.0b 只靠兩個浮點函式符號——因為兩版的 C 函式庫差異模組沒有被這個 binary 連結到。Ghidra 中累積的每一個確認的 CRT function 都是一個新的判別點。

**執行方式：** 逐一比對的部分依 [ADR-0007](../../../docs/adr/0007-workflow-automation-and-agent-context.md) 由 workflow 跑完，每次 agent 呼叫只帶一個 CRT function、把比對結果寫成判定檔、只回傳摘要。收斂版本結論是整體推理，由 workflow 最後一段統合判定檔完成，不是逐項工作。

本票沒有 Ghidra 寫入，gate 換成「每個判定檔都有明確的比對依據，不得只寫結論」。

**Blocked by:** 14

**Status:** ready-for-agent

- [ ] 以 Ghidra 中已確認的 CRT function 逐一比對各候選版本的函式庫
- [ ] 10.0a 與 10.0b 的區分補強，或明確記錄仍無法區分及其原因
- [ ] 「函式庫版本等於編譯器版本」這個推論設法驗證，或明確標記為未驗證
- [ ] 判定結論與完整證據鏈進知識庫
- [ ] workflow 有錯誤處理：agent 未回傳或判定檔缺漏會重試、落地與 gate 失敗會明確回報、上游工具失去回應有停止訊號；收尾報告列出完成數、失敗數與未完成清單
- [ ] 若結論與原判定不符，明確記錄並評估對已完成工作的影響
