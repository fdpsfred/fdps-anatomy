# tools/cut_items — 道具的取得途徑普查

[`cut_content/items.md`](../../cut_content/items.md)（票 25.12）的「拿不到的道具」由這裡從遊戲檔產生並驗證。它是對整張物品表的普查，不是逐件判定：程式把物品放進背包的每一條途徑是一條規則，每條規則照 `src/` 轉錄，途徑清單與程式出處寫在主題檔的「取得途徑」一節。單元測試把主題檔寫的件數與類型釘住。

前作沒有對應成果：`fd2-anatomy` 沒有刪減與未用的資料夾，也沒有物品取得途徑的普查。資料的解讀沿用本專案的擁有者：名稱、商店、轉職路線與「有沒有村莊」取自 [`data_tables`](../data_tables/_index.md)，地圖的部署記錄、可搜尋格記錄與格子類別取自 [`map_decode`](../map_decode/_index.md)。

## 檔案

| 檔案 | 用途 |
| --- | --- |
| `acquisition.py` | 三個子命令（見下） |
| `test_acquisition.py` | 單元測試：`python -m unittest tools/cut_items/test_acquisition.py`。規則以小型合成資料逐條測；有 `workspace/vfs_dump/` 時另跑一次完整普查，檢查 226／171／55／32 四個數、六個拿不到的裝備類型，以及主題檔的表與資料一致 |

## 子命令

```
python tools/cut_items/acquisition.py summary [--dump DIR]
python tools/cut_items/acquisition.py table   [--dump DIR]
python tools/cut_items/acquisition.py check   [--dump DIR]
```

- `summary`：有內容的物品、拿得到、拿不到、連部署記錄都沒帶的件數，以及我方職業能裝、卻沒有一件拿得到的裝備類型。
- `table`：拿不到的物品表（Markdown），每件列出它在資料裡出現、但都到不了的地方。
- `check`：重新產生那張表，與 `cut_content/items.md` 的表逐格比對，有差異就以非零結束。改了主題檔的表或規則之後要跑到 `OK`。

`DIR` 預設 `workspace/vfs_dump`（[`vfs_dump`](../vfs_dump/_index.md) 的輸出），與 `data_tables`、`map_decode` 相同。

## 寫在程式裡的知識

下列幾樣不是從資料檔讀的，而是從 `src/` 轉錄成常數，改之前要回去讀原始碼：

- 各章事件以常數給的物品（`GRANTS`）：每個以常數呼叫 `fdps_unit_add_item` 的地方，連同所在檔案。
- 抽獎：三種大獎發不出來、只發藥草（`src/vilbar.c`）。
- 神秘商店的暗號表只有 24 列（`src/village.c`），章節索引 25 的那一列讀到表外。
- 入隊裝備取 `FRIAPRDA.DAT` 角色 `00`–`0B` 的 `+0x0C`–`+0x11`（`src/roster.c`）；戰鬥中的轉交只收陣營 2（`src/item.c`）。
