# 職業 record

`PROMAP.DAT` 的一筆，10 byte。數值見 [`assets/classes.md`](../classes.md)，檔案層面的事實見 [`resource_info/data_tables.md`](../../resource_info/data_tables.md)。

| 偏移 | 型別 | 欄位 | 內容 |
| ---: | --- | --- | --- |
| `0x00` | `u8[8]` | `move_cost` | 八種地形各自的行動力消耗，`FF` 表示不可通行 |
| `0x08` | `u8` | `critical` | 暴擊率（百分比） |
| `0x09` | `u8` | `magic_resist_complement` | 100 減去魔法抗性 |

## 索引是職業代碼加 1

取值函式 `0x18b70` 算的是 `base + index * 10`，而呼叫端傳進去的是「職業代碼 + 1」——`MOV AL, [record + 0x20]` 取執行期人物 record 的職業代碼，`INC EAX`，再呼叫。第 0 筆是八個地形一律 1、暴擊 0、魔抗補數 0 的預設 record，`0x11e50` 與 `0x13040` 這類不針對特定單位的呼叫直接傳 0 取它。
