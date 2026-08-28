# code_emit — 把 function emit 成 C 的流程

一次一個 function：emitter 讀三源產出 C 與測試、reviewer 從 assembly 獨立驗證、build gate 判定、bookkeeper 記帳並 commit。流程的結論、角色分工與檢查項目由 [`rebuild_info/emit_pipeline.md`](../../rebuild_info/emit_pipeline.md) 擁有，本檔只講腳本與跑法。

建置本身不在這裡重寫——`build_emit.py` 匯入 [`tools/fdps_build/`](../fdps_build/_index.md) 的機制（前置檢查、conf／批次檔產生、三訊號結束偵測、故障掃描），只換掉編譯清單與執行階段的內容。

## 腳本

| 檔案 | 用途 |
| --- | --- |
| `emit_ticket22.js` | 票 22 的 Workflow 編排，遊戲本體全部 function。開跑第一段先收拾上一輪被殺後留下的殘骸，再去問 `next_batch.py` 要工作清單，序列跑 emit → review → 修正迴圈 → gate → commit，超出行數預算時插入拆檔段，收尾做疑慮回掃與 devlog。錯誤處理含單項重試、上游失效偵測、gate 失敗的修復迴圈、未完成項目的工作區清理與記帳。不看自己的預算，跑完呼叫者給的數量為止 |
| `emit_ticket21.js` | 票 21 的版本，工作清單由 `args` 帶入、沒有拆檔段。留著當該票的紀錄，新工作用票 22 那支 |
| `build_emit.py` | 四個子命令。`build` 把 `src/` 與 `tests/` 全部編譯**連結兩次**成 `EMITTEST.EXE`；`run` 在 DOSBox-X 裡執行它並讀回測試紀錄；`all`（預設）依序跑兩者；`selftest` 不碰 DOSBox-X，驗證接線產生、紀錄解析與兩段式連結的判定 |
| `gen_stubs.py` | 產生第二次連結用的零填充 stub 模組。輸入是第一次連結報出的未定義符號，型別與大小取自 `data/routing.json`。`--selftest` 驗型別對應與該拒絕的三類符號 |
| `gen_types.py` | 從 `ghidra_snapshot/data_types.txt` 產生 `src/fdpstype.h` 與 `tests/fdpstype.c`。`--check` 只驗不寫，`--selftest` 驗洞的填補與錯誤佈局的拒絕 |
| `emit_order.py` | 從 call graph 算出 callee 先於 caller 的 emit 順序，產出 `workspace/code_emit/emit_order.json` |
| `next_batch.py` | 以 `data/routing.json` 為名冊、`data/emit_state.json` 為進度，依 `emit_order.json` 的順序產生要交給 workflow 的工作清單，並比對 Ghidra 快照退休過期的判定。`--stats` 看進度 |
| `DumpRoutingInputs.java` | Ghidra script，唯讀。把 routing 規劃要的整體事實匯出成兩個 JSON：每支 `pool_fdps` function 的反編譯行數、caller／callee、碰到的具名資料，以及每個具名資料符號的取用者 |
| `build_routing.py` | routing 判定表的所在，產生 `data/routing.json` 與 `data/routing.md`。`--stats` 看每檔行數，`--check` 只驗不寫 |

## 資料

| 檔案 | 內容 |
| --- | --- |
| `data/routing.json` | 每支 function 與每個遊戲全域的目標 `.c`，加上不 emit 的符號清單。由 `build_routing.py` 產生，不手改 |
| `data/routing.md` | 同一份路由的逐檔清單，人讀用。同樣是產生物 |
| `data/emit_state.json` | 進度的正本，進版控。續跑的唯一依據。**不記檔案落點** |
| `data/emit_issues.json` | 尚未收斂的等價性疑慮，一個 function 一組。由 bookkeeper 累加、由回掃段更新 |

`workspace/code_emit/` 下的兩個產出值得單獨提：`undefined.json` 是第一次連結報出的未定義符號，也就是票 23 的權威工作清單，每次建置重新產生；`emit_order.json` 是 callee 先於 caller 的 emit 順序，call graph 變動後重跑 `emit_order.py` 更新。

落點的判定依據與超標處置規則由 [`rebuild_info/code_layout.md`](../../rebuild_info/code_layout.md) 擁有，本目錄只放表與產生器。

判定檔（emitter 與 reviewer 的完整產出）落在 `workspace/code_emit/verdicts/`，`DumpRoutingInputs.java` 的輸出落在 `workspace/code_emit/routing_inputs/`，中間產物與建置產出落在 `workspace/code_emit/` 其餘位置。

## 跑法

```
python tools/code_emit/build_routing.py --check       # 路由表還成立嗎
python tools/code_emit/gen_types.py --check           # fdpstype.h 還跟快照一致嗎
python tools/code_emit/next_batch.py --stats          # 還剩多少
python tools/code_emit/build_emit.py all              # emitter 用的快速迴圈
python tools/build_gate/gate.py check --target emittest   # 正式的 gate
```

Workflow 自己去拿工作清單，呼叫時只給批次大小：

```
Workflow({ scriptPath: "tools/code_emit/emit_ticket22.js",
           args: { limit: 40, batchLabel: "t22-01" } })
```

**可重跑**：`emit_state.json` 是進度的正本，每支通過的 function 各自一個 commit，所以任何中斷最多損失飛在半空的那一支。跑完一次就再呼叫一次，它會從 `next_batch.py` 拿到接下來的一批。

被 usage limit 就地殺掉也一樣：下一次呼叫的第一段（Recover）自己丟掉 `src/`／`tests/` 的殘骸、把飛在半空的那一支從 `in_flight` 改成 `interrupted` 送回工作清單，不需要人先去 `git status`。界線與理由見 [`rebuild_info/emit_pipeline.md`](../../rebuild_info/emit_pipeline.md) 的「中斷復原」。`src/`／`tests/` 以外的地方髒了它會停下來報告而不是自行處理，那時才需要人。

Call graph 改變後（新建或刪除 function）要重算順序：

```
run_ghidra_script  tools/call_graph/BuildCallGraph.java
python tools/code_emit/emit_order.py
```

## 注意

- **接線是推導出來的，不能手改。** 編譯清單就是 `src/` 與 `tests/` 現有的檔，測試進入點的 runner 清單就是各 `tests/<stem>.c` 真的定義了的 `run_<stem>_tests`。沒有需要維護的建置檔，新增測試檔就是新增測試的全部工作。
- **`selftest` 要一起跑**，而且已經排進 gate 的測試套件。它證明「接線產生」與「紀錄解析」都會失敗——一個永遠註冊不到 runner 的產生器，或一個把失敗的執行讀成乾淨的解析器，會讓整個閘門在壞掉的程式碼上說 PASS。
- **`build_emit.py` 一定要前景跑。** agent 一旦送出最終訊息就結束，背景建置沒有人接得到結果。
- 原始碼暫存成兩個 guest 目錄 `C:\SRC` 與 `C:\TST`，物件檔分別落在 `OBJS\` 與 `OBJT\`。理由是測試檔與生產檔同名（`tests/menu.c` 對 `src/menu.c`），攤平在同一個目錄會互相覆蓋。
- 暫存區每次重建。沿用舊的會讓已經從版本庫刪掉或改名的檔案繼續從殘留副本被編進去。
- 光碟映像在時就掛、不在就不掛。測試映像沒有任何東西讀光碟，把它變成硬相依會讓「這台機器沒有光碟映像」被報成閘門失敗。
- **連結跑兩次，第一次的未定義符號是預期產物而不是錯誤。** 第一次不帶 stub，報出來的就是「已 emit 的程式碼要、但還沒有人定義」的完整清單，落檔到 `workspace/code_emit/undefined.json`；第二次帶上 `gen_stubs.py` 產生的零填充模組，必須乾淨。判定用的是第二次的結果。
- **stub 模組不進 `src/`。** 它每次建置重新產生，落在暫存區。放進 `src/` 就會與真的 emit 出來的定義混在一起，一百支 function 之後分不出誰是誰。
- **`src/fdpstype.h` 與 `tests/fdpstype.c` 是產生物。** 改 struct 佈局要改 Ghidra、重匯出快照、重跑 `gen_types.py`，不手改這兩個檔。
