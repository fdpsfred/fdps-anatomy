# 22 — 全 function emit

**What to build:** 遊戲本體的每個 function 都有對應的、經過獨立審查的 C 原始碼。做完這張票，程式邏輯的重建就完成了。

**執行方式：** 沿用票 21 建好的 workflow，不另外設計，規模放大到全部遊戲本體 function。[ADR-0007](../../../docs/adr/0007-workflow-automation-and-agent-context.md) 的四條在這裡是硬需求而非建議——這是全專案規模最大的逐項工作，任何「把完整產出帶回 orchestrator」的寫法都撐不到終點。

這張票是最需要「全自動跑完、不中途回來確認」的一張：以每個 function 一次確認計，人工節奏會讓工期完全被回覆延遲支配。

**Blocked by:** 21

**Status:** ready-for-agent

- [ ] 遊戲本體 function 全部 emit 完成，每個都經 reviewer 通過
- [ ] 每個 function 一次處理一個，無任何批次處理
- [ ] 全程無人介入跑完，可中斷可續跑，重跑跳過已完成的 function
- [ ] workflow 有錯誤處理：agent 未回傳或判定檔缺漏會重試、落地與 gate 失敗會明確回報、上游工具失去回應有停止訊號；收尾報告列出完成數、失敗數與未完成清單
- [ ] 純機械計算的 function 有單元測試，期望值來自攻略公式或 Ghidra emulator，非臆測
- [ ] 讀取真實遊戲檔的測試讀真檔，不捏造假檔
- [ ] 過程中發現的 Ghidra 描述錯誤當場修正並同步知識庫
- [ ] 無法當下確認的等價性疑慮明確記錄，不遺漏，並在回掃段以已完成的鄰近 function 重讀一次
- [ ] 絕無半成品、無為遷就測試而扭曲的程式碼
- [ ] 每個工作段落有對應的 devlog
