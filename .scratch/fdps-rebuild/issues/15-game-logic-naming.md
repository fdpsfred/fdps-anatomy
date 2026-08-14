# 15 — 遊戲邏輯 function 命名與逐一 cc 判定

**What to build:** 遊戲本體的每個 function 都有語意正確的名稱、正確的 calling convention、以及描述其行為的註解。做完這張票，Ghidra 中就不再有任何預設命名，這個程式對人類是可讀的。

calling convention 是逐 function 判定的——個別 function 會偏離預設值，必須在這裡確認，因為 emit 時要在程式碼中明確宣告。

**Blocked by:** 13, 07, 14

**Status:** ready-for-agent

- [ ] 每個遊戲本體 function 逐一讀過 assembly 後命名，一次處理一個
- [ ] 每個 function 的 calling convention 逐一判定，偏離預設值的明確標記
- [ ] 命名採用專案詞彙表的語彙，並參照攻略基準真值確認語意正確
- [ ] 每個 function 有描述其行為的註解
- [ ] 未被辨識的程式碼區域、跳躍表、直落到下一個 function 的情形全部處理完畢
- [ ] 完成判定：零個預設命名殘留、零個錯誤標記
- [ ] Ghidra 快照匯出並 commit
