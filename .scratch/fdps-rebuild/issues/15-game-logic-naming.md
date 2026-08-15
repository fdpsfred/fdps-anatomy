# 15 — 遊戲邏輯 function 命名與逐一 cc 判定

**What to build:** 遊戲本體的每個 function 都有語意正確的名稱、正確的 calling convention、以及描述其行為的註解。做完這張票，Ghidra 中就不再有任何預設命名，這個程式對人類是可讀的。

calling convention 是逐 function 判定的——個別 function 會偏離預設值，必須在這裡確認，因為 emit 時要在程式碼中明確宣告。

**執行方式：** 依 [ADR-0007](../../../docs/adr/0007-workflow-automation-and-agent-context.md) 的五條原則，本票寫自己的 workflow。與票 12 的差別：工作清單來自票 14 產出的遊戲本體 function 清單，是固定的，不需要 BFS 發現；規模是票 12 的數倍，所以判定檔寫檔控 context 與錯誤處理都更關鍵——這個量級一定會有 agent 失敗，靜默跳過就等於漏掉 function 而看不出來。

票 12 已經定案的 109 個 function 不重做，以判定檔的存在跳過。`tools/backbone_walk/` 的 `collect_verdicts.py` 與 `ApplyBackboneWalk.java` 這類小工具可以照抄或改寫，但 workflow 本身自己寫。其中記錄的工具坑要沿用：`__watcall` 與 `const` 都不能寫進 prototype 字串、標記 no-return 會產生孤立程式碼、Ghidra 的 `__watcall` 預設標籤與 `-4s` 的事實相反。

**Blocked by:** 07, 14

**Status:** ready-for-agent

- [ ] 每個遊戲本體 function 逐一讀過 assembly 後命名，一次處理一個
- [ ] 每個 function 的 calling convention 逐一判定，偏離預設值的明確標記
- [ ] 命名採用專案詞彙表的語彙，並參照攻略基準真值確認語意正確
- [ ] 每個 function 有描述其行為的註解
- [ ] 命名過程中新發現的跳躍表與直落到下一個 function 的情形全部處理完畢（成批的未辨識程式碼已在 14 號票建成 function）
- [ ] 完成判定：零個預設命名殘留、零個錯誤標記
- [ ] 回掃段跑完，沒有殘留低信心或未決的判定；仍無解的明確列出
- [ ] 全程無人介入跑完，agent 的 context 用量不隨處理量成長
- [ ] workflow 有錯誤處理：agent 未回傳或判定檔缺漏會重試、落地與 gate 失敗會明確回報、上游工具失去回應有停止訊號；收尾報告列出完成數、失敗數與未完成清單
- [ ] Ghidra 快照匯出並 commit
