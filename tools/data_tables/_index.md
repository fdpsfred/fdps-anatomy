# data_tables — 資料表的產生與驗證

從遊戲檔直接產生 [`assets/`](../../assets/_index.md) 裡幾張表，並把知識庫的表逐格對回資料。數值取自 `MISC.VFS`／`FIELD.VFS` 成員的實際 byte，名稱取自 `FDETXT00.TXT`（經 [`text_decode`](../text_decode/_index.md) 解出，條目基底就是程式自己加的常數），部署記錄經 [`map_decode`](../map_decode/_index.md) 解析；攻略站一概不讀。

```
python tools/data_tables/data_tables.py gen   [--dump DIR] [--out DIR]
python tools/data_tables/data_tables.py apply [--dump DIR]
python tools/data_tables/data_tables.py check [--dump DIR]
python -m unittest tools/data_tables/test_data_tables.py
```

`DIR` 預設 `workspace/vfs_dump`（[`vfs_dump`](../vfs_dump/_index.md) 的輸出）。

## 三個子命令

- **`gen`**：把每張表寫成 Markdown 到 `workspace/data_tables/<表>.md`，只看不動知識庫。
- **`apply`**：把表寫進知識庫。找到表頭完全相同的表就整張換掉，否則換掉 `<!-- data_tables:<表> -->` 那一行（第一次放表時用）；表外的文字不動。
- **`check`**：重新產生每張表，與知識庫裡的表逐格比對；另外把 `items.md`、`spells.md`、`classes.md` 的名稱欄與 `characters.md` 兩張表的人物名對遊戲內文字。有任何不同就列出並以非零結束。這是本工具的閘門，單元測試也跑它。

## 產生的表

| 表 | 寫進 | 內容 |
| --- | --- | --- |
| `enemies` | `assets/enemies.md` | `ENEMYDAT.DAT` 91 列加上名稱與部署到的章、過場地圖 |
| `races` | `assets/races.md` | 種族代碼、名稱與屬於它的單位 |
| `class_users` | `assets/classes.md` | 每個職業有哪些單位 |
| `promotions` | `assets/characters.md` | `RANKUP.DAT` 的四條路線 |
| `spell_schedule` | `assets/characters.md` | 每人每型態的初始法術與升級習得 |
| `shops` | `assets/shops.md` | 有村莊的 20 個 `SHOPnn.DAT` |
| `use_effects` | `assets/items.md` | 使用效果代碼與帶這個碼的物品 |

## 寫在程式裡的知識

有三樣東西不是從資料檔讀的，而是從 `src/` 轉錄成常數，改之前要回去讀原始碼：

- 名稱條目的基底（`NAME_BASES`）與「角色編號 + 1」的偏置。
- 哪些章節索引沒有村莊（`no_village`，照 `CHAPTER_HAS_NO_VILLAGE`），以及只有蘭迪斯能走勇者徽章路線。
- 使用效果代碼的說明文字（`USE_EFFECTS`），照 `fdps_apply_item_effect_to_targets`。`ITEM.DAT` 出現說明表裡沒有的碼時，產生直接失敗，不會默默少一列。

## 對應的前作成果

前作 `fd2-anatomy/assets/` 的敵人、種族、譯名表是人工轉錄，沒有產生器；`fd2-anatomy/tools/growth_table/` 只做成長表的網頁。本工具只沿用它們的涵蓋面，資料格式與產生方式照 FDPS 自己的檔案寫。
