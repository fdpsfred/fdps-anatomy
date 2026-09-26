# 職業 record

以職業代碼索引的兩張表：`PROMAP.DAT` 的地形消耗 record，與 `PROEQU.DAT` 的可裝備類型 record。數值見 [`assets/classes.md`](../classes.md)，檔案層面的事實見 [`resource_info/data_tables.md`](../../resource_info/data_tables.md)。

## `PROMAP.DAT` 的一筆，10 byte

| 偏移 | 型別 | 欄位 | 內容 |
| ---: | --- | --- | --- |
| `0x00` | `u8[8]` | `move_cost` | 八種地形各自的行動力消耗，`FF` 表示不可通行 |
| `0x08` | `u8` | `critical` | 暴擊率（百分比） |
| `0x09` | `u8` | `magic_resist_complement` | 100 減去魔法抗性 |

### 索引是職業代碼加 1

取值函式 `0x18b70` 算的是 `base + index * 10`，而呼叫端傳進去的是「職業代碼 + 1」——`MOV AL, [record + 0x20]` 取執行期人物 record 的職業代碼，`INC EAX`，再呼叫。第 0 筆是八個地形一律 1、暴擊 0、魔抗補數 0 的預設 record，`fdps_collect_targets_in_range`（`0x11e50`）、`fdps_map_actor_score_best_item`（`0x13040`）、`fdps_map_actor_score_best_spell`（`0x13420`）三個不針對特定單位的呼叫直接傳 0 取它。唯一的例外是 `fdps_map_actor_move_toward_nearest_reachable_opponent`（`0x126b0`）：它取了職業代碼沒有 `INC` 就呼叫，拿到的是職業代碼少 1 那個職業的列，職業 `0x00` 則拿到預設 record；這是原版 bug，見 [已知原版 bug](../../program_info/known_bugs.md)。

## `PROEQU.DAT` 的一筆，6 byte

一個職業准許裝備的物品類型代碼。六個 byte 不是六個固定欄位，而是一個**變長集合**：用到的類型代碼由小到大排在前面，用不到的位置填 `0xFF`。

| 偏移 | 型別 | 欄位 | 內容 |
| ---: | --- | --- | --- |
| `0x00` | `u8[6]` | `allowed_item_types` | 准許的物品類型代碼，升冪排列，空位為 `0xFF` |

代碼比對的對象是物品 record 的 `+0x00` 類型欄位（見 [`items.md`](items.md)）。每個職業的准許類型見 [`assets/classes.md`](../classes.md)。

### 索引是原始的職業代碼，沒有加 1

取值函式 `0x18ba0` 算的是 `base + index * 6`，呼叫端傳的是人物 record `+0x20` 的職業代碼**原值**——`PROMAP.DAT` 那個 +1 的偏置在這裡不存在，因為 `PROEQU.DAT` 沒有領頭的預設 record，職業 `0x00` 就是第 0 筆。

### 沒有 sentinel，也沒有任何界限

唯一的讀取端 `fdps_unit_can_equip_item` 六格全掃，不因為讀到 `0xFF` 就停；空位的 `0xFF` 不是任何物品 record 的類型值；`0x00` 雖然不是有內容物品的類型，卻是 `ITEM.DAT` 裡 `0xE2`–`0xFA` 那 25 筆全零 record 的類型，拿它填空位會讓有空位的職業都准許這些 record。取值函式本身也什麼都不檢查：216 byte 只裝得下職業代碼 `0x00`–`0x23` 的 36 筆，而職業代碼一路到 `0x27`，所以 `0x24`–`0x27` 會讀到檔案後面；乘法是帶號的，負的索引會讀到檔案前面；表基底也不測 null。重建時照直覺改寫會錯在哪，見 [`rebuild_info/pitfalls.md`](../../rebuild_info/pitfalls.md)。

### 兩張表的越界讀取在遊戲中走不到

人物 record `+0x20` 的職業碼只有四個寫入點：部署時取 `FRIAPRDA.DAT` 或 `ENEMYDAT.DAT`、入隊時取 `FRIAPRDA.DAT`、教會轉職時取 `RANKUP.DAT`。三檔的職業碼上限分別是 `0x19`、`0x26`、`0x18`。能走到 `fdps_unit_can_equip_item` 的只有隊伍成員與 side 2 的單位，職業碼最大 `0x19`，所以 `PROEQU.DAT` 的 36 筆夠用。`0x24`–`0x26` 只出現在敵方單位身上，它們只查 `PROMAP.DAT`，列號最大 `0x27`，落在 41 筆之內。
