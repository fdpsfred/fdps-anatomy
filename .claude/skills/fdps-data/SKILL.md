---
name: fdps-data
description: 查 FDPS（炎龍騎士團外傳）的遊戲數值——物品、法術、人物出場屬性與成長、職業、章節。在 Ghidra 或資料檔裡看到一個數字、一個代碼、一個名稱想知道它是什麼時使用。
---

# FDPS 遊戲資料查詢

`fdps_data.json` 是從 `MISC.VFS` 的實際 byte 產生的資料集，涵蓋五張表：

| 表 | 筆數 | 來源成員 | 正典 |
| --- | ---: | --- | --- |
| `item` | 251（226 筆有內容） | `ITEM.DAT` | `assets/items.md` |
| `spell` | 40 | `MAGICDAT.DAT` | `assets/spells.md` |
| `character` | 60（34 筆有內容，其中 31 筆有人名） | `FRIAPRDA.DAT`／`FRILEVUP.DAT`／`GETMGTAB.DAT` | `assets/characters.md` |
| `class` | 40 | `PROMAP.DAT` | `assets/classes.md` |
| `chapter` | 30 | — | `chapters/_index.md` |

## 查詢

```
python .claude/skills/fdps-data/query.py <指令> ...
```

| 指令 | 用途 |
| --- | --- |
| `show <表> <代碼>` | 一筆完整內容，含來源與已知的攻略站歧異 |
| `list <表> [--limit N] [--blank]` | 整張表，一筆一行 |
| `name <文字> [--table 表]` | 名稱含該文字的記錄，跨表 |
| `where <表> <欄位=值> ...` | 依數值篩選，`ap=300..400`、`mp=..20`、`type=0x07` |
| `find <數值> [--table 表]` | 全表全欄位反查哪裡出現這個數字 |
| `fields [表]` | `where` 能用的欄位名 |

代碼在 `item`／`spell`／`character`／`class` 是十六進位，`chapter` 是十進位（與知識庫的寫法一致）；`0x` 前綴或 `--dec` 可以覆蓋。任何指令加 `--json` 得到原始記錄。

```
python .claude/skills/fdps-data/query.py show item 4A
python .claude/skills/fdps-data/query.py where spell mp=..20 hit=95..100
python .claude/skills/fdps-data/query.py find 0x2710
```

## 讀結果時要知道的事

- **數值一律出自資料檔**。物品、法術、人物、職業的名稱出自遊戲內文字，章節名稱出自攻略站，都經知識庫轉錄（物品名與攻略站寫法的三處差異列在 `assets/items.md`，人物與單位的見 `assets/names.md`）。數值與攻略站不一致的地方以資料檔為準，`show` 會在該筆下面用 `!` 印出攻略站的說法。全部歧異列在 `fdps_data.json` 的 `discrepancies`。
- **`blank` 的記錄預設不列出**：知識庫的表完全沒有列到的索引，`item` 的 `E2`–`FA` 與人物的空白索引都屬於這類。要看加 `--blank`。人物的 `0C` 索爾、`22`、`23`「？？？？」、`3B` 侍衛不是 `blank`——它們有成長內容；`22` 在遊戲內沒有名字，名稱欄顯示「（空白）」。
- **人物的出場屬性只有 `00`–`0B` 十二筆**。其餘索引的 `appearance_documented` 是 `false`，出場欄位一律 `null`——`FRIAPRDA.DAT` 那些位置的 byte 不是空的：`0C` 索爾、`0D` 卡里斯、`0E` 亞雷斯、`23`、`24`–`27`、`3B` 侍衛是非隊員角色的出場屬性，其餘是沒有讀取端的複本，資料集目前沒有收（見 `assets/characters.md`）。這些索引仍有成長範圍與法術習得。
- **`distance` 與 `use_distance` 是位元欄位**：設了 `0x10` 表示直線，低 4 位才是距離。`distance_line` 與 `distance_value` 是解好的。
- **法術的 `power` 為負數時是攻擊力加乘率**，`ap_multiplier` 是換算好的倍率。
- **職業的 `code` 是職業代碼，`record_index` 才是它在 `PROMAP.DAT` 的位置**，兩者差 1。
- 物品的 `use_effect` 碼只輸出原始值，它的意思查 `assets/items.md` 的「使用效果代碼」。其他沒有標名稱的代碼欄位（物品的 `type`、`use_target` 等）沒有解讀出的對照表，不要自行推測含義。
- 資料集只收上面五張表；敵方單位、種族、商店、轉職路線查 `assets/enemies.md`、`assets/races.md`、`assets/shops.md`、`assets/characters.md`。

## 重新產生

資料檔或知識庫的表更新後重跑：

```
python tools/data_skill/build.py fdps_game_files/MISC.VFS .claude/skills/fdps-data/fdps_data.json
```

建置時會拿知識庫每一列的數值去對資料檔的 record，對不上就中止——所以這份資料集不會與 `assets/` 漂移。細節見 `tools/data_skill/_index.md`。
