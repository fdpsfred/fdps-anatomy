# tools — 工作腳本

每個子資料夾對應一項工作。腳本 self-contained，不 import 共用函式庫。

儲存慣例：腳本放 `tools/{工作名稱}/`，所有可重生的中間產物與輸出放 `workspace/{工作名稱}/`。知識庫不得引用 `workspace/` 下的路徑。唯一的例外是 Ghidra 文字快照，它的輸出本身就是要進版控的資料，寫到 `ghidra_snapshot/`。

| 子資料夾 | 用途 |
| --- | --- |
| [`cd_scope/`](cd_scope/_index.md) | 透過 DOSBox-X 把光碟映像的內容複製出來並清點 |
| [`ghidra_snapshot/`](ghidra_snapshot/_index.md) | 把 Ghidra 的分析狀態匯出成文字快照 |
