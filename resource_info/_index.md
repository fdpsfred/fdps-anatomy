# resource_info — 檔案視角

回答「每個資源檔的二進位格式是什麼」。一個檔對應一種格式或一組實體檔案，並是該格式的唯一正典。

| 文件 | 內容 |
| --- | --- |
| [`cel.md`](cel.md) | `.CEL` sprite 圖表的欄位佈局、兩種像素編碼，與 99 個檔的 sprite 數與尺寸 |
| [`data_tables.md`](data_tables.md) | `MISC.VFS` 內九個 `.DAT` 資料表的大小、record 大小、筆數與索引方式，以及攻略站偏移的歸屬 |
| [`disc_images.md`](disc_images.md) | 兩片光碟的內容清單（檔名、大小、SHA-256、音軌表）與各檔案在遊戲中的角色 |
| [`map.md`](map.md) | `FIELD.VFS` 的 `MAPnn.DAT`／`MAPnn.COD`：命名與載入、回合事件、格子事件與部署記錄的佈局、部署座標、地圖單位索引的排法 |
| [`saf.md`](saf.md) | `.SAF` 動畫容器的四層結構（frame／tilemap／tile／音效）與 525 個檔的內容統計 |
| [`save.md`](save.md) | `FDE.SAV` 存檔：live-state 與 4 個 slot 的逐欄佈局與各欄的讀寫 function、空 slot 標記與不會被寫到的 byte、檢查碼與 XOR 加密的算法與誰驗、以實際存檔驗證的結果 |
| [`text.md`](text.md) | `FIELD.VFS` 的 `FDETXTnn.TXT` 文字區塊與 token、`FDETXT00.TXT` 的佈局、`FDETXT.FON` 字模表 |
| [`vfs.md`](vfs.md) | `.VFS` 容器的欄位佈局、程式端實際讀取的範圍，與 10 個容器的全部成員清單 |
