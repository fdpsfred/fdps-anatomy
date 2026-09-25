# assets/text — 遊戲文字

回答「遊戲裡的字與文字內容是什麼」。承載文字的檔案格式（`FDETXTnn.TXT` 的 offset 表與 token、`FDETXT.FON` 的字模佈局）由 [`resource_info/text.md`](../../resource_info/text.md) 擁有，這裡只寫內容。

遊戲文字不是 Big5：每個 token 是 `FDETXT.FON` 的字模索引，要經過本層的字模對照表才得到字。解碼器是 [`tools/text_decode/`](../../tools/text_decode/_index.md)，讀的是本層的 `glyph_table.json`。

| 文件 | 內容 |
| --- | --- |
| [`glyph_table.md`](glyph_table.md) | `FDETXT.FON` 1,792 個字模索引各代表哪個字，與每個字的判定依據（倚天字型逐像素吻合或開發者判讀）；機器可讀的同一份是 `glyph_table.json` |
