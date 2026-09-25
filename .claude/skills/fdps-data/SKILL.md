---
name: fdps-data
description: 查 FDPS（炎龍騎士團外傳）的遊戲資料——物品、法術、人物出場屬性與成長、職業、敵人、種族、商店、使用效果代碼，每一章的敵人與波次、寶物、事件、過場腳本，以及全部 66 個文字區塊的全文搜尋與「這條文字由誰顯示、為什麼不會顯示」。在 Ghidra 或資料檔裡看到一個數字、一個代碼、一個名稱或一句台詞想知道它是什麼時使用。
---

# FDPS 遊戲資料查詢

兩份資料集都從遊戲檔現解產生：

- `fdps_data.json`：九張表。

  | 表 | 筆數 | 代碼 | 來源 | 正典 |
  | --- | ---: | --- | --- | --- |
  | `item` | 251（226 筆有內容） | 物品編號 | `ITEM.DAT` | `assets/items.md` |
  | `spell` | 40 | 法術編號 | `MAGICDAT.DAT` | `assets/spells.md` |
  | `character` | 60（41 筆有內容） | 肖像編號 `00`–`3B` | `FRIAPRDA.DAT`／`FRILEVUP.DAT`／`GETMGTAB.DAT`／`RANKUP.DAT` | `assets/characters.md` |
  | `class` | 40 | 職業代碼 | `PROMAP.DAT` | `assets/classes.md` |
  | `enemy` | 91 | 肖像編號 `3C`–`96` | `ENEMYDAT.DAT` | `assets/enemies.md` |
  | `race` | 7 | 種族代碼 | `FDETXT00.TXT` | `assets/races.md` |
  | `shop` | 21 | 章節索引（十進位） | `SHOPnn.DAT` | `assets/shops.md` |
  | `use_effect` | 29 | 使用效果代碼 | `ITEM.DAT` +0x0d 與 `src/item.c` | `assets/items.md` |
  | `chapter` | 30 | 章號（十進位） | `MAPnn.DAT`、過場腳本、章節文字區塊、逐章判定 | `chapters/` |

- `fdps_text.json`：`FDETXT00`–`FDETXT65` 全部 1,746 條文字，每條附狀態（`shown` 顯示、`never_shown` 永遠不會顯示、`empty` 空字串）、讀取端，以及永遠不會顯示的條目歸哪個 `cut_content/` 條目。

## 查詢

```
python .claude/skills/fdps-data/query.py <指令> ...
```

| 指令 | 用途 |
| --- | --- |
| `show <表> <代碼>` | 一筆完整內容，含來源與已知的攻略站歧異；`show chapter N` 等於 `chapter N` |
| `list <表> [--limit N] [--blank]` | 整張表，一筆一行 |
| `name <文字> [--table 表]` | 名稱含該文字的記錄，跨表 |
| `where <表> <欄位=值> ...` | 依數值篩選，`ap=300..400`、`mp=..20`、`type=0x07` |
| `find <數值> [--table 表]` | 全表全欄位反查哪裡出現這個數字（`chapter` 除外） |
| `fields [表]` | `where` 能用的欄位名 |
| `chapter <章號> [--enemies] [--treasure] [--events] [--scripts] [--text]` | 一章：章名、勝敗條件、處理函式，再加指定的段落；不指定就印前四段 |
| `deploy <肖像編號>` | 一個單位在 30 章地圖上的全部部署記錄，含波次與會不會出場 |
| `text <文字> [--block N] [--status 狀態] [--limit N]` | 全文搜尋，換行、換頁不打斷比對 |
| `entry <區塊> <條目>` | 一條文字的全文、讀取端，或它為什麼不會顯示、歸哪個 `cut_content/` 條目 |

代碼在 `item`／`spell`／`character`／`class`／`enemy`／`race`／`use_effect` 是十六進位，`chapter`／`shop` 是十進位（與知識庫的寫法一致）；`0x` 前綴或 `--dec` 可以覆蓋。文字的區塊是十進位（`FDETXTnn` 的 nn）、條目是十六進位。任何指令加 `--json` 得到原始記錄。

```
python .claude/skills/fdps-data/query.py show item 4A
python .claude/skills/fdps-data/query.py where enemy ap=20..
python .claude/skills/fdps-data/query.py chapter 28 --enemies
python .claude/skills/fdps-data/query.py deploy 0C
python .claude/skills/fdps-data/query.py text 平衡之神 --status never_shown
python .claude/skills/fdps-data/query.py entry 27 0x15
```

## 讀結果時要知道的事

- **數值與名稱都出自遊戲檔**：名稱是遊戲內文字 `FDETXT00` 的名稱條目，章名與勝敗條件是各章文字區塊的表頭。與攻略站寫法的差異見 `assets/names.md`；數值與攻略站不一致的地方以資料檔為準，`show` 會在該筆下面用 `!` 印出攻略站的說法，全部歧異列在 `fdps_data.json` 的 `discrepancies`。資料本身判斷不了的事——波次會不會部署、文字由誰顯示、為什麼不會顯示——取自章節頁的逐章判定與 `cut_content/` 的歸屬，`provenance` 寫明每一類的來源。
- **`blank` 的記錄預設不列出**：知識庫的表完全沒有列到的索引，`item` 的 `E2`–`FA` 與人物的空白索引都屬於這類。要看加 `--blank`。人物的 `22` 有成長內容但遊戲內沒有名字，名稱欄是空的；`24`–`27` 是第 23 章的友方群眾，同樣沒有名字。
- **人物的出場屬性只給程式會讀到的索引**：`00`–`0B` 十二名我方人物，加上部署記錄讀到的非隊員角色 `0C` 索爾、`0D` 卡里斯、`0E` 亞雷斯、`23`「？？？？」、`24`–`27`、`3B` 侍衛。其餘索引的 `appearance_documented` 是 `false`，出場欄位一律 `null`——那些位置的 byte 是沒有讀取端的複本（見 `assets/characters.md`「非隊員角色與沒有讀取端的索引」），轉職型態 `0F`–`21` 只有成長與習得。非隊員角色經部署建立時，等級、法術與物品取部署記錄，不是出場屬性的那三欄。
- **轉職路線掛在人物上**：`character` 的 `promotions` 是 `RANKUP.DAT` 教會實際提供的路線，勇者徽章只有 `00` 蘭迪斯有。
- **敵人與人物的 `deployed_chapters` 只表示有部署記錄**，不表示一定出場；某筆記錄會不會出場看 `deploy` 或 `chapter --enemies` 的波次判定（`never deployed` 的波次由 `cut_content/` 擁有）。`deployed_scene_maps` 是過場地圖（地圖編號 31 以上），以地圖編號列出。
- **`distance` 與 `use_distance` 是位元欄位**：設了 `0x10` 表示直線，低 4 位才是距離。`distance_line` 與 `distance_value` 是解好的。
- **法術的 `power` 為負數時是攻擊力加乘率**，`ap_multiplier` 是換算好的倍率。
- **職業的 `code` 是職業代碼，`record_index` 才是它在 `PROMAP.DAT` 的位置**，兩者差 1。
- **`shop` 的代碼是檔名裡的章節索引**：`SHOP01.DAT` 是第 1 章勝利後、第 2 章之前的村莊。
- 物品的 `use_effect` 碼的意思在 `use_effect_description`，完整說明是 `use_effect` 表。其他沒有標名稱的代碼欄位（物品的 `type`、`use_target` 等）沒有解讀出的對照表，不要自行推測含義。
- **文字的 `lines`**：【名稱】是換說話者（括號裡是角色編號），每一項是一行，▼ 是換頁；`{subst1}`、`{subst2}`、`{number}` 是執行期代入。`readers` 在全域區塊是程式裡的函式（名稱區以代碼索引，只寫區域與函式數），在章節區塊是過場腳本步驟、處理函式或死亡腳本，在額外場景區塊是過場腳本步驟。
- 資料集沒有收的：地圖地形、格子屬性（`tools/map_decode`）、過場腳本的逐步內容（`chapters/chNN.md` 的「過場腳本」）、存檔格式（`resource_info/`）。

## 重新產生

遊戲檔或知識庫的表更新後重跑：

```
python tools/data_skill/build.py
```

建置時會拿知識庫每一列的數值去對資料檔的 record，並跑各頁產生器自己的閘門（資料表、全域文字、`cut_content/story.md`、章節頁），任何一處對不上就中止、不寫檔——所以這份資料集不會與知識庫漂移。細節見 `tools/data_skill/_index.md`。
