# 14 — 未辨識程式碼建 function 與 vendor pool 判定

**What to build:** 每個 function 是不是遊戲本體，有了明確答案。CRT、AIL、以及編譯器產物這三個 pool 的成員被逐一判定出來，剩下的才是需要 emit 成 C 的遊戲邏輯。

判定的前置是**先把 function 集合補齊**。票 10 的基準盤點留下 485 個沒有任何 reference 的未反組譯區塊、共 39,652 byte，其中 32,556 byte 在程式庫區（`0x3c000` 之後）、7,096 byte 在遊戲邏輯區。抽樣顯示多數是完整的函式——對齊填充之後接 Watcom 的 `53 56 57 55 89 e5 81 ec` prologue——只是呼叫圖走不到，所以自動分析從來沒有碰過它們。這批程式碼不先建成 function，pool 判定就會漏掉八成的量，而且漏掉的都在最需要判 pool 的程式庫區。清單見 `program_info/memory_layout.md`，可用 `tools/ghidra_baseline/AuditGhidraBaseline.java` 重新產生。

這三個 pool 的判定不需要遊戲資料當依據——它們靠 function 本身的特徵、字串參照、以及與已知函式庫的比對就能確認，所以不必等攻略基準真值。

**執行方式：** 依 [ADR-0007](../../../docs/adr/0007-workflow-automation-and-agent-context.md) 的五條原則，本票寫自己的 workflow。每次 agent 呼叫只帶一個區塊或一個 function；agent 把完整判定寫成檔案、只回傳摘要；判定階段不寫 Ghidra，建 function 與打 pool 標記集中在轉錄階段；每輪落地後跑基準稽核；結束前回掃；錯誤要處理不得靜默跳過。

不要照抄票 12 的 BFS：本票的工作清單是 `AuditGhidraBaseline.java` 產生的未反組譯區塊清單與全 function 清單，是固定的，不需要逐輪發現。但**建 function 與 pool 判定必須分成兩個階段**——建完 function 之後 function 集合才完整，pool 判定的清單才算數。這個兩段結構是本票專屬的，票 12 沒有對應物。

**Blocked by:** 12

**Status:** done

- [x] 未辨識為程式碼的區域逐一判定是程式碼還是資料，一次處理一個；是程式碼的反組譯並建成 function，是資料的定義型別並記錄
- [x] 建完之後重跑基準稽核，確認孤立程式碼與 error bookmark 仍是 0
- [x] 每個 function 逐一讀過 assembly 後判定 pool 歸屬，不使用位址範圍圈定
- [x] CRT pool 成員辨識出來並以函式庫比對佐證
- [x] AIL pool 成員辨識出來，邊界明確
- [x] 編譯器產物（不對應任何原始碼的 function）辨識出來
- [x] 剩餘的遊戲本體 function 清單產出，作為 15 號票的工作清單
- [x] 判定結果與依據記錄在 Ghidra 的標記與註解中
- [x] 全程無人介入跑完，agent 的 context 用量不隨處理量成長
- [x] workflow 有錯誤處理：agent 未回傳或判定檔缺漏會重試、落地與 gate 失敗會明確回報、上游工具失去回應有停止訊號；收尾報告列出完成數、失敗數與未完成清單
- [x] Ghidra 快照匯出並 commit

## 結果

- 未定義區塊 738 段逐一判定：填充 578、程式碼 125、資料 77、混合 20。`.object1` 未定義 byte 從 43,143 降到 **0**，基準稽核因此多一個門檻值。
- function 1,042 → **1,347**（新建 305 個），全部有 pool 判定：`fdps` 533、`ail` 423、`crt` 379、`binary_artifact` 12，沒有 `unknown`。
- CRT 以 Ghidra Function ID 對 Watcom 10.0 家族的 `CLIB3S`／`MATH387S`／`EMU387` 比對，196 個命中且無一落到其他 pool。AIL 靠連結方向與 debug build 的 API 字串定案，還原出 185 個 Miles 原始 API 名稱。
- 票 15 的工作清單 = Ghidra 內 `pool_fdps` 這 533 個 function，隨快照進版控；知識庫不另存副本。
- 結論頁 [`program_info/code_pools.md`](../../../program_info/code_pools.md)，敘事在 `devlog/2026-08-15-pool-triage-blocks.md` 與 `devlog/2026-08-15-pool-triage.md`。

**「全程無人介入」的實際情形**：分成 4 次 workflow 呼叫，每次都自己從頭跑到底、中途不回來要人確認；分段是預算切的（撞過 agent 上限與 session token 上限各一次），不是決策切的。四次中斷都沒有資料遺失，經驗已寫回 [ADR-0007](../../../docs/adr/0007-workflow-automation-and-agent-context.md) 第五條。
