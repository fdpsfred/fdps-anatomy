# tools — 工作腳本

每個子資料夾對應一項工作。腳本 self-contained，不 import 共用函式庫。

儲存慣例：腳本放 `tools/{工作名稱}/`，所有可重生的中間產物與輸出放 `workspace/{工作名稱}/`。知識庫不得引用 `workspace/` 下的路徑。例外是本身就要進版控的產物：Ghidra 文字快照寫到 `ghidra_snapshot/`，攻略站鏡像寫到 `docs/guide/`。

| 子資料夾 | 用途 |
| --- | --- |
| [`cd_scope/`](cd_scope/_index.md) | 透過 DOSBox-X 把光碟映像的內容複製出來並清點 |
| [`cel_decode/`](cel_decode/_index.md) | 解出 `.CEL` 的每個 sprite 並算圖成 PNG |
| [`ghidra_snapshot/`](ghidra_snapshot/_index.md) | 把 Ghidra 的分析狀態匯出成文字快照 |
| [`guide_scrape/`](guide_scrape/_index.md) | 把攻略站的內容頁抓成原文鏡像並提供搜尋入口 |
| [`vfs_dump/`](vfs_dump/_index.md) | 解開 `.VFS` 容器並驗證其自洽性 |
