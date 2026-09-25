# map_decode — 地圖圖層與地圖腳本解碼、全圖算圖

把一張地圖的事件碼層、場景圖層、屬性表、圖層描述與 `MAPnn.DAT`／`MAPnn.COD` 讀進來，照遊戲的讀法判讀每一格，並把整張地圖算成 PNG。格式結論記在 [`resource_info/terrain.md`](../../resource_info/terrain.md) 與 [`resource_info/map.md`](../../resource_info/map.md)。

`.CEL` 圖磚的解碼 import 擁有者 [`cel_decode`](../cel_decode/_index.md)，不另寫一份。

## `map_decode.py`

```
python tools/map_decode/map_decode.py check   [<vfs_dump 目錄>]
python tools/map_decode/map_decode.py show    <地圖編號> [<vfs_dump 目錄>]
python tools/map_decode/map_decode.py render  <輸出目錄> [--annotate] [--maps 0,7,..] [<vfs_dump 目錄>]
python tools/map_decode/map_decode.py report  [<vfs_dump 目錄>]
```

`<vfs_dump 目錄>` 預設是 `workspace/vfs_dump`，也就是 [`vfs_dump`](../vfs_dump/_index.md) 解開的容器。

- `check`：解析全部 64 張地圖，逐條檢查知識庫依賴的不變量（事件碼層比地形層寬且蓋得住、事件碼只到 15、屬性表列數對圖磚表、動畫幀在圖磚表內、`COD` 筆數等於部署加我方 slot⋯），印出每條成立與否及例外。檔案格式不符（magic、大小）直接中止。
- `show`：印一張地圖的全部內容——各層描述、回合事件、每個寶箱／埋藏／重繪格／格子事件格與它指到的記錄、16 筆可搜尋格記錄、全部部署記錄。物品名稱取自 [`fdps-data`](../../.claude/skills/fdps-data/SKILL.md) 的資料集（沒有就只印編號）。
- `render`：每張地圖一張 `Mnn.png`，每格 24×24。與視窗同步捲動的圖層照遊戲的順序（深度由小到大、深度 10 不畫）疊起來；視差圖層另存 `Mnn_L<層>.png`。加 `--annotate` 另存 `Mnn_annotated.png`，框出寶箱（黃，`C<碼>`）、埋藏（橘，`B<碼>`）、重繪格（青，`R<碼>`）、格子事件格（洋紅，`E<碼>`）與我方起始格（綠，`P<slot>`）。圖磚一律取第 0 幀；`M310.CEL` 依它自己的編碼解，而遊戲不會這樣畫它（[`resource_info/cel.md`](../../resource_info/cel.md)）。
- `report`：印出 [`resource_info/map.md`](../../resource_info/map.md) 的「每張地圖的統計」表。

模組函式可以直接 import：`load_map`、`tile_info`、`classify_cell`、`searchable_cells`、`render_map` 是 25.8 章節檔與 25.14 刪減場景要用的介面。

## 測試

```
python -m unittest tools/map_decode/test_map_decode.py
```

以手工組出的 byte 驗證各解析器與「一格的讀法」，另在有解開的遊戲檔時，以攻略站第 1 章的寶物清單驗證真實資料。

## 執行

```
python tools/vfs_dump/vfs_dump.py dump fdps_game_files workspace/vfs_dump
python tools/map_decode/map_decode.py check
python tools/map_decode/map_decode.py report > workspace/map_decode/report.md
python tools/map_decode/map_decode.py render workspace/map_decode/png
```
