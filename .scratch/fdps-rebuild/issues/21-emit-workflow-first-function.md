# 21 — Emit workflow 腳本與首個 function 試跑

**What to build:** 把一個 function 從 Ghidra 的分析結果變成 C 原始碼的完整流程走通一次。流程是序列的——emitter 產出、獨立的 reviewer 驗證、有問題就進修正迴圈、通過後獨立 commit。序列是必要的：reviewer 依賴版本控制的暫存狀態來檢視本輪改動，這要求一次只有一個 function 在飛。

**Blocked by:** 15, 17, 19, 20

**Status:** ready-for-agent

- [ ] 腳本驅動 emitter、reviewer、修正迴圈、記帳四個角色，每次呼叫只帶一個 function
- [ ] emitter 必須取得三源（plate comment、disassembly、decompiled C）才能作業
- [ ] reviewer 獨立驗證，不信任 emitter 也不信任 decompiled C，結論自行從 assembly 確認
- [ ] reviewer 能看到 emitter 本輪的精確改動
- [ ] 每個 function 的 calling convention 在程式碼中明確宣告
- [ ] 產出的 C 為 C89、檔名符合 8.3 限制、符號名與 Ghidra 完全一致
- [ ] 每個 function 通過後獨立 commit，工作狀態記錄可續跑
- [ ] 前作記錄的八類實機 bug 列入 reviewer 的檢查項目
- [ ] 至少一個 function 完整走通並通過 build gate
