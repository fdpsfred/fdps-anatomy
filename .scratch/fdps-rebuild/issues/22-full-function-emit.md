# 22 — 全 function emit

**What to build:** 遊戲本體的每個 function 都有對應的、經過獨立審查的 C 原始碼。做完這張票，程式邏輯的重建就完成了。

**Blocked by:** 21

**Status:** ready-for-agent

- [ ] 遊戲本體 function 全部 emit 完成，每個都經 reviewer 通過
- [ ] 每個 function 一次處理一個，無任何批次處理
- [ ] 純機械計算的 function 有單元測試，期望值來自攻略公式或 Ghidra emulator，非臆測
- [ ] 讀取真實遊戲檔的測試讀真檔，不捏造假檔
- [ ] 過程中發現的 Ghidra 描述錯誤當場修正並同步知識庫
- [ ] 無法當下確認的等價性疑慮明確記錄，不遺漏
- [ ] 絕無半成品、無為遷就測試而扭曲的程式碼
- [ ] 每個工作段落有對應的 devlog
