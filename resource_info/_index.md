# resource_info — 檔案視角

回答「每個資源檔的二進位格式是什麼」。一個檔對應一種格式或一組實體檔案，並是該格式的唯一正典。

| 文件 | 內容 |
| --- | --- |
| [`cel.md`](cel.md) | `.CEL` sprite 圖表的欄位佈局、兩種像素編碼，與 99 個檔的 sprite 數與尺寸 |
| [`cutscene_script.md`](cutscene_script.md) | `ICONANI.VFS` 的 66 支過場腳本（`ICONnn`、`WINnn`、`GOODEND` 等）：誰在什麼時機播放、opcode 的長度與作用與使用統計、切換地圖時文字區塊與單位陣列怎麼跟著變、原版的越界寫入，以及每支腳本切到的地圖與用到的文字區塊條目 |
| [`data_tables.md`](data_tables.md) | `MISC.VFS` 內九個 `.DAT` 資料表的大小、record 大小、筆數與索引方式，以及攻略站偏移的歸屬 |
| [`disc_images.md`](disc_images.md) | 兩片光碟的內容清單（檔名、大小、SHA-256、音軌表）與各檔案在遊戲中的角色 |
| [`map.md`](map.md) | `FIELD.VFS` 的 `MAPnn.DAT`／`MAPnn.COD`：命名與載入、回合事件的陣營階段與觸發時機、事件碼的三種用法（可搜尋格、重繪、格子事件）、可搜尋格記錄的種類與搜尋流程、格子事件的觸發時機、部署記錄的佈局、AI 行為代碼、死亡腳本 opcode、部署座標、地圖單位索引的排法，以及 64 張地圖的統計表 |
| [`saf.md`](saf.md) | `.SAF` 動畫容器的四層結構（frame／tilemap／tile／音效）與 525 個檔的內容統計 |
| [`save.md`](save.md) | `FDE.SAV` 存檔：live-state 與 4 個 slot 的逐欄佈局與各欄的讀寫 function、空 slot 標記與不會被寫到的 byte、檢查碼與 XOR 加密的算法與誰驗、以實際存檔驗證的結果 |
| [`terrain.md`](terrain.md) | 地圖圖層 `Mnn.DTL`（事件碼層）、`Mnnn.MPL`（圖磚編號）、`ATTRnnn.DAT`（圖磚屬性）、`DSCnn.DAT`（圖層描述）的佈局與程式的讀法：一格怎麼判讀（事件碼層用自己的寬）、屬性旗標與地形類別、圖層的繪製規則、哪些地圖缺檔而載入不了 |
| [`text.md`](text.md) | `FIELD.VFS` 的 `FDETXTnn.TXT` 文字區塊與 token、`FDETXT00.TXT` 的佈局、`FDETXT.FON` 字模表 |
| [`vfs.md`](vfs.md) | `.VFS` 容器的欄位佈局、程式端實際讀取的範圍，與 10 個容器的全部成員清單 |
