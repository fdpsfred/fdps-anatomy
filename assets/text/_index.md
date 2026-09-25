# assets/text — 遊戲文字

回答「遊戲裡的字與文字內容是什麼」。承載文字的檔案格式（`FDETXTnn.TXT` 的 offset 表與 token、`FDETXT.FON` 的字模佈局）由 [`resource_info/text.md`](../../resource_info/text.md) 擁有，這裡只寫內容。

遊戲文字不是 Big5：每個 token 是 `FDETXT.FON` 的字模索引，要經過本層的字模對照表才得到字。解碼器是 [`tools/text_decode/`](../../tools/text_decode/_index.md)，讀的是本層的 `glyph_table.json`。

`FDETXT01`–`FDETXT30` 是各章自己的文字，內容記在各章頁 [`chapters/`](../../chapters/_index.md)。永遠不會顯示的文字不在本層列出，由 [`cut_content/`](../../cut_content/_index.md) 收錄。

| 文件 | 內容 |
| --- | --- |
| [`glyph_table.md`](glyph_table.md) | `FDETXT.FON` 1,792 個字模索引各代表哪個字，與每個字的判定依據（倚天字型逐像素吻合或開發者判讀）；機器可讀的同一份是 `glyph_table.json` |
| [`global_text.md`](global_text.md) | `FDETXT00.TXT` 全域文字 555 條：單位、種族、職業、物品、法術名稱表與系統訊息的分區、每區的取條目公式（敵方單位名是「肖像編號 + 1」）與 `src/` 裡的讀取端，以及名稱表越界讀到下一張表的後果；由 [`tools/global_text/`](../../tools/global_text/_index.md) 產生 |
| [`scene_text.md`](scene_text.md) | `FDETXT31`–`FDETXT65` 額外場景（地圖 30–64）的文字：每個區塊由哪支過場腳本切入、每條由哪支腳本在哪個偏移顯示，以及為什麼只有腳本會顯示它們；由 [`tools/global_text/`](../../tools/global_text/_index.md) 產生 |
