# pool_rereview — 全 pool 逐 function 覆核

把票 14 對每一個 function 下的判定重讀一次：pool 歸屬、符號名稱、邊界與 signature、plate comment，四個軸各自獨立重判，錯的當場修掉。這是票 14.2 專屬的 workflow，不是給別票用的框架（[ADR-0007](../../docs/adr/0007-workflow-automation-and-agent-context.md)）。

| 檔案 | 用途 |
| --- | --- |
| `rereview_ticket14_2.js` | 主 workflow：重讀 → 落地 → 兩道 gate → 回掃 → 重算知識庫與 devlog，全程無人介入 |
| `DumpRereviewState.java` | 匯出每個 function 的事實與現況，兩份分開，另出反組譯，唯讀 |
| `build_packets.py` | 攤成一個 function 一份證據包與一份現況包，並算出還沒覆核的清單 |
| `ApplyRereviewVerdicts.java` | 把覆核判定轉錄進 Ghidra：pool tag、改名、簽章、plate comment、邊界修正 |
| `AuditNames.java` | 本票專屬的 gate：vendor 符號撞名、名稱前綴與 pool 衝突、pool tag 數量 |

## 執行

```
Workflow({ scriptPath: "tools/pool_rereview/rereview_ticket14_2.js",
           args: { functionIds: [...], roundSize: 30, maxFunctions: 900 } })
```

工作清單長達 1,347 個位址，讓 agent 讀檔再複述回來只是浪費生成，所以正常做法是先在本機跑

```bash
python tools/pool_rereview/build_packets.py
```

從 `workspace/pool_rereview/worklist.json` 取 `todo`，再放進 `args.functionIds`。**可重跑**：已有判定檔且判定涵蓋範圍與現況相符的 function 不會再判一次。

## 先讀證據，再讀答案

覆核的意思是重讀，不是複查標記。同一個 agent 若先看到現況的 pool tag，之後做的就只是替既有判定找理由——那樣跑一千次也只會確認原判。

強制的手段是把匯出拆成兩份檔案：`items/facts/` 只有位元組、body range 與洞、body 之後的空隙、caller/callee、指向進入點的全部參照、資料與字串參照、兩組 Function ID 命中；`items/current/` 才是 Ghidra 現在宣稱的名稱、tag、簽章與 plate comment。prompt 規定閱讀順序，判定檔的 `independent` 欄位記下 agent 在讀現況之前得到的結論，事後可以稽核。

## 邊界改動會讓鄰居的判定過期

改動一個 function 的 body 就改變了它鄰居的身分，鄰居先前那份判定描述的範圍已經不存在——即使鄰居自己的位元組沒動、body 雜湊還一樣。

`ApplyRereviewVerdicts.java` 因此把每一次邊界改動附加寫進 `boundary_changes.jsonl`，`build_packets.py` 把落在該範圍內的判定檔退休，讓那些 function 重回工作清單。每一行只消費一次，進度記在 `boundary_applied.json`，重建不會反覆退休同一批鄰居。

續跑的另一半是雜湊比對：判定檔的 `covers.body_sha` 與現況不符就退休成 `.superseded-<舊 sha>`，function 不存在了就退休成 `.orphan`。

## gate 有兩道

基準稽核（`tools/ghidra_baseline/AuditGhidraBaseline.java`）：孤立程式碼 0、error bookmark 0、`.object1` 未定義 byte 0。

命名稽核（`AuditNames.java`）：**沒有兩個位址搶同一個 vendor 符號**，名稱前綴不與 pool 衝突，每個 function 剛好一個 `pool_*` tag。撞名不是外觀問題——名字是 wlink 拿去解析真正 `.LIB` 的依據，也是 pool 判定的證據本身，所以兩個位址claim 同一個符號代表其中一邊的識別是錯的。落地腳本遇到撞名一律原樣保留並回報，不自行挑一個贏家，也不加位址後綴。

Ghidra 自己產生的名稱（`FUN_`、`thunk_FUN_`）撞在一起不算數，那不代表任何人的判斷。

## 錯誤處理

[ADR-0007](../../docs/adr/0007-workflow-automation-and-agent-context.md) 第五條的實作對照：

| 狀況 | 行為 |
| --- | --- |
| 單一 agent 沒回傳或沒寫判定檔 | 重試一次，仍失敗記進未完成清單並繼續 |
| 一輪裡的 agent 全部沒回傳 | 判定為上游失效，立刻停止並列出未動過的項目，不送重試 |
| 落地腳本遇到做不了的判定 | 原樣保留、回報原因，不自行改判定 |
| gate 不過 | 該輪修復，修不掉回報 `ok: false` |
| Ghidra 沒回應 | apply agent 回報 `ghidra_responding: false`，立刻停止 |
| 已停止 | 不寫知識庫、不產下游清單，但仍輸出收尾報告 |
