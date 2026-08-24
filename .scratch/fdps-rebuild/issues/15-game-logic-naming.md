# 15 — 遊戲邏輯 function 命名與逐一 cc 判定

**What to build:** 遊戲本體的每個 function 都有語意正確的名稱、語意正確的參數名稱、正確的 calling convention、以及描述其行為的註解。做完這張票，Ghidra 中就不再有任何預設命名，這個程式對人類是可讀的。

calling convention 是逐 function 判定的——個別 function 會偏離預設值，必須在這裡確認，因為 emit 時要在程式碼中明確宣告。

參數名稱由該參數在 function 內的實際用途決定，依據是 assembly 中對它的讀寫與傳遞，佐以 caller 傳入的實參來源。命名與 function 名稱同用專案詞彙表的語彙。

**執行方式：** 依 [ADR-0007](../../../docs/adr/0007-workflow-automation-and-agent-context.md) 的五條原則，本票寫自己的 workflow。與票 12 的差別：工作清單來自票 14.2 覆核後的遊戲本體 function 清單，是固定的，不需要 BFS 發現；規模是票 12 的數倍，所以判定檔寫檔控 context 與錯誤處理都更關鍵——這個量級一定會有 agent 失敗，靜默跳過就等於漏掉 function 而看不出來。

票 12 已經定案的 109 個 function 只補參數命名——它的判定檔的 `params` 欄位記的是傳參證據（哪個暫存器、哪個堆疊位移），不是語意名稱，所以名稱、cc 與註解沿用，參數名要在本票補齊。這批以判定檔存在但缺參數名為識別條件，走一條只判參數的輕量路徑。`tools/backbone_walk/` 的 `collect_verdicts.py` 與 `ApplyBackboneWalk.java` 這類小工具可以照抄或改寫，但 workflow 本身自己寫。其中記錄的工具坑要沿用：`__watcall` 與 `const` 都不能寫進 prototype 字串、標記 no-return 會產生孤立程式碼、Ghidra 的 `__watcall` 預設標籤與 `-4s` 的事實相反。

**Blocked by:** 07, 14.2

**Status:** done

- [x] 每個遊戲本體 function 逐一讀過 assembly 後命名，一次處理一個
- [x] 每個 function 的 calling convention 逐一判定，偏離預設值的明確標記
- [x] 每個 function 的每個參數依其實際用途命名，與 function 名稱在同一次判定中完成
- [x] 命名採用專案詞彙表的語彙，並參照攻略基準真值確認語意正確
- [x] 每個 function 有描述其行為的註解
- [x] 命名過程中新發現的跳躍表與直落到下一個 function 的情形全部處理完畢（成批的未辨識程式碼已在 14 號票建成 function）
- [x] 完成判定：零個預設命名殘留（含 `FUN_*` 與 `param_N`）、零個錯誤標記
- [x] 回掃段跑完，沒有殘留低信心或未決的判定；仍無解的明確列出
- [x] 全程無人介入跑完，agent 的 context 用量不隨處理量成長
- [x] workflow 有錯誤處理：agent 未回傳或判定檔缺漏會重試、落地與 gate 失敗會明確回報、上游工具失去回應有停止訊號；收尾報告列出完成數、失敗數與未完成清單
- [x] Ghidra 快照匯出並 commit
