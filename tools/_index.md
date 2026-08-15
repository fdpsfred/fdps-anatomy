# tools — 工作腳本

每個子資料夾對應一項工作。腳本 self-contained，不 import 共用函式庫。

儲存慣例：腳本放 `tools/{工作名稱}/`，所有可重生的中間產物與輸出放 `workspace/{工作名稱}/`。知識庫不得引用 `workspace/` 下的路徑。例外是本身就要進版控的產物：Ghidra 文字快照寫到 `ghidra_snapshot/`，攻略站鏡像寫到 `docs/guide/`，遊戲資料查詢 skill 的資料集寫到 `.claude/skills/fdps-data/`。

| 子資料夾 | 用途 |
| --- | --- |
| [`backbone_walk/`](backbone_walk/_index.md) | 從進入點到主迴圈逐一走查並命名：workflow 每個 agent 只讀一個 function，落地由 orchestrator 轉錄 |
| [`build_flags/`](build_flags/_index.md) | 反推建置旗標組：解 LE header、跨 Watcom 版本差分編譯、CRT 位元組比對、連結實驗 |
| [`call_graph/`](call_graph/_index.md) | 建出呼叫圖（含函式指標表的間接邊）並算可達性、孤島分量與共用 helper 排名 |
| [`cd_scope/`](cd_scope/_index.md) | 透過 DOSBox-X 把光碟映像的內容複製出來並清點 |
| [`cel_decode/`](cel_decode/_index.md) | 解出 `.CEL` 的每個 sprite 並算圖成 PNG |
| [`data_skill/`](data_skill/_index.md) | 產生 `fdps-data` 查詢 skill 的資料集，並對知識庫的表逐列驗證 |
| [`ghidra_baseline/`](ghidra_baseline/_index.md) | 複查 Ghidra 基準狀態：區塊屬性、孤立程式碼、未反組譯區域、error bookmark |
| [`ghidra_config/`](ghidra_config/_index.md) | Ghidra MCP 專案設定的正本：把命名檢查調成本專案的慣例 |
| [`ghidra_snapshot/`](ghidra_snapshot/_index.md) | 把 Ghidra 的分析狀態匯出成文字快照 |
| [`guide_offsets/`](guide_offsets/_index.md) | 把攻略站給的資料表偏移對回 `MISC.VFS` 成員，解表並與攻略站數值逐筆比對 |
| [`guide_scrape/`](guide_scrape/_index.md) | 把攻略站的內容頁抓成原文鏡像並提供搜尋入口 |
| [`saf_decode/`](saf_decode/_index.md) | 解出 `.SAF` 的四層結構、驗證自洽性，並算圖成 PNG 與 WAV |
| [`vfs_dump/`](vfs_dump/_index.md) | 解開 `.VFS` 容器並驗證其自洽性 |
