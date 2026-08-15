# 21 — Emit workflow 腳本與首個 function 試跑

**What to build:** 把一個 function 從 Ghidra 的分析結果變成 C 原始碼的完整流程走通一次。流程是序列的——emitter 產出、獨立的 reviewer 驗證、有問題就進修正迴圈、通過後獨立 commit。序列是必要的：reviewer 依賴版本控制的暫存狀態來檢視本輪改動，這要求一次只有一個 function 在飛。

**執行方式：** 依 [ADR-0007](../../../docs/adr/0007-workflow-automation-and-agent-context.md)，但**併行度為 1**。ADR-0007 的四條照樣適用，序列只是把「一次一個」的併行度收到底：

- **判定寫檔、只回傳摘要**——emitter 與 reviewer 的完整產出（產生的 C、審查意見、未決疑慮）寫成檔案，回傳給腳本的只有結果與是否通過。以 600 個以上 function 的規模，把審查意見全帶回 orchestrator 必然爆 context。
- **落地與 gate 分離**——emit 的 gate 是 build gate（編譯連結零錯誤零新增警告加測試全過），每個 function 通過後才 commit。
- **回掃**——emit 過程中記下的等價性疑慮，在該批結束前重讀一次；此時相鄰 function 已經 emit 完，當時看不出來的契約可能已經明朗。

序列的理由不變：reviewer 依賴版本控制的暫存狀態檢視本輪改動。若之後要提高併行度，必須先解決 reviewer 的檢視範圍問題（例如 worktree 隔離），那是獨立的決定，不在本票範圍。

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
- [ ] emitter 與 reviewer 的完整產出寫成檔案，回傳腳本的只有摘要
- [ ] workflow 有錯誤處理：agent 未回傳或判定檔缺漏會重試、落地與 gate 失敗會明確回報、上游工具失去回應有停止訊號；收尾報告列出完成數、失敗數與未完成清單
- [ ] 至少一個 function 完整走通並通過 build gate
