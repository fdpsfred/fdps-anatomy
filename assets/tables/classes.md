# 職業 record

以職業代碼索引的兩張表：`PROMAP.DAT` 的地形消耗 record，與 `PROEQU.DAT` 的可裝備類型 record。數值見 [`assets/classes.md`](../classes.md)，檔案層面的事實見 [`resource_info/data_tables.md`](../../resource_info/data_tables.md)。

## `PROMAP.DAT` 的一筆，10 byte

| 偏移 | 型別 | 欄位 | 內容 |
| ---: | --- | --- | --- |
| `0x00` | `u8[8]` | `move_cost` | 八種地形各自的行動力消耗，`FF` 表示不可通行 |
| `0x08` | `u8` | `critical` | 暴擊率（百分比） |
| `0x09` | `u8` | `magic_resist_complement` | 100 減去魔法抗性 |

### 索引是職業代碼加 1

取值函式 `0x18b70` 算的是 `base + index * 10`，而呼叫端傳進去的是「職業代碼 + 1」——`MOV AL, [record + 0x20]` 取執行期人物 record 的職業代碼，`INC EAX`，再呼叫。第 0 筆是八個地形一律 1、暴擊 0、魔抗補數 0 的預設 record，`0x11e50` 與 `0x13040` 這類不針對特定單位的呼叫直接傳 0 取它。

## `PROEQU.DAT` 的一筆，6 byte

一個職業准許裝備的物品類型代碼。六個 byte 不是六個固定欄位，而是一個**變長集合**：用到的類型代碼由小到大排在前面，用不到的位置填 `0xFF`。

| 偏移 | 型別 | 欄位 | 內容 |
| ---: | --- | --- | --- |
| `0x00` | `u8[6]` | `allowed_item_types` | 准許的物品類型代碼，升冪排列，空位為 `0xFF` |

代碼比對的對象是物品 record 的 `+0x00` 類型欄位（見 [`items.md`](items.md)）。六個代碼各自對應哪一種物品尚未解讀。

### 索引是原始的職業代碼，沒有加 1

取值函式 `0x18ba0` 算的是 `base + index * 6`，呼叫端傳的是人物 record `+0x20` 的職業代碼**原值**——`PROMAP.DAT` 那個 +1 的偏置在這裡不存在，因為 `PROEQU.DAT` 沒有領頭的預設 record，職業 `0x00` 就是第 0 筆。

### 沒有 sentinel，也沒有任何界限

唯一的讀取端 `fdps_unit_can_equip_item` 六格全掃，不因為讀到 `0xFF` 就停；空位用 `0xFF` 而不是 `0x00`，是因為 `0x00` 本身就是一個活的物品類型代碼。取值函式本身也什麼都不檢查：216 byte 只裝得下職業代碼 `0x00`–`0x23` 的 36 筆，而職業代碼一路到 `0x27`，所以 `0x24`–`0x27` 會讀到檔案後面；乘法是帶號的，負的索引會讀到檔案前面；表基底也不測 null。重建時照直覺補上終止判斷會改變行為，見 [`rebuild_info/pitfalls.md`](../../rebuild_info/pitfalls.md)。
