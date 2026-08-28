# code_emit — 把 function emit 成 C 的流程

一次一個 function：emitter 讀三源產出 C 與測試、reviewer 從 assembly 獨立驗證、build gate 判定、bookkeeper 記帳並 commit。流程的結論、角色分工與檢查項目由 [`rebuild_info/emit_pipeline.md`](../../rebuild_info/emit_pipeline.md) 擁有，本檔只講腳本與跑法。

建置本身不在這裡重寫——`build_emit.py` 匯入 [`tools/fdps_build/`](../fdps_build/_index.md) 的機制（前置檢查、conf／批次檔產生、三訊號結束偵測、故障掃描），只換掉編譯清單與執行階段的內容。

## 腳本

| 檔案 | 用途 |
| --- | --- |
| `emit_ticket21.js` | Workflow 編排。序列跑完工作清單：emit → review → 修正迴圈 → gate → commit，收尾做疑慮回掃與 devlog。錯誤處理含單項重試、上游失效偵測、gate 失敗的修復迴圈、未完成項目的工作區清理與記帳 |
| `build_emit.py` | 四個子命令。`build` 把 `src/` 與 `tests/` 全部編譯連結成 `EMITTEST.EXE`；`run` 在 DOSBox-X 裡執行它並讀回測試紀錄；`all`（預設）依序跑兩者；`selftest` 不碰 DOSBox-X，驗證接線產生與紀錄解析 |
| `next_batch.py` | 從 `data/emit_state.json` 產生要交給 workflow 的工作清單，並比對 Ghidra 快照退休過期的判定。`--stats` 看進度 |

## 資料

| 檔案 | 內容 |
| --- | --- |
| `data/emit_state.json` | 進度與檔案落點的正本，進版控。續跑的唯一依據 |
| `data/emit_issues.json` | 尚未收斂的等價性疑慮，一個 function 一組。由 bookkeeper 累加、由回掃段更新 |

判定檔（emitter 與 reviewer 的完整產出）落在 `workspace/code_emit/verdicts/`，中間產物與建置產出落在 `workspace/code_emit/` 其餘位置。

## 跑法

```
python tools/code_emit/next_batch.py --stats          # 還剩多少
python tools/code_emit/next_batch.py --limit 1        # 產生 workflow 的 args
python tools/code_emit/build_emit.py all              # emitter 用的快速迴圈
python tools/build_gate/gate.py check --target emittest   # 正式的 gate
```

Workflow 用 `next_batch.py` 印出來的 JSON 當 `args` 呼叫：

```
Workflow({ scriptPath: "tools/code_emit/emit_ticket21.js", args: <上一步的 JSON> })
```

## 注意

- **接線是推導出來的，不能手改。** 編譯清單就是 `src/` 與 `tests/` 現有的檔，測試進入點的 runner 清單就是各 `tests/<stem>.c` 真的定義了的 `run_<stem>_tests`。沒有需要維護的建置檔，新增測試檔就是新增測試的全部工作。
- **`selftest` 要一起跑**，而且已經排進 gate 的測試套件。它證明「接線產生」與「紀錄解析」都會失敗——一個永遠註冊不到 runner 的產生器，或一個把失敗的執行讀成乾淨的解析器，會讓整個閘門在壞掉的程式碼上說 PASS。
- **`build_emit.py` 一定要前景跑。** agent 一旦送出最終訊息就結束，背景建置沒有人接得到結果。
- 原始碼暫存成兩個 guest 目錄 `C:\SRC` 與 `C:\TST`，物件檔分別落在 `OBJS\` 與 `OBJT\`。理由是測試檔與生產檔同名（`tests/menu.c` 對 `src/menu.c`），攤平在同一個目錄會互相覆蓋。
- 暫存區每次重建。沿用舊的會讓已經從版本庫刪掉或改名的檔案繼續從殘留副本被編進去。
- 光碟映像在時就掛、不在就不掛。測試映像沒有任何東西讀光碟，把它變成硬相依會讓「這台機器沒有光碟映像」被報成閘門失敗。
