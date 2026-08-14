# 人物 record

三張表各自的一筆。數值見 [`assets/characters.md`](../characters.md)，檔案層面的事實見 [`resource_info/data_tables.md`](../../resource_info/data_tables.md)。

## 出場屬性 `FRIAPRDA.DAT`，24 byte

| 偏移 | 型別 | 欄位 | 內容 |
| ---: | --- | --- | --- |
| `0x00` | `u8` | `race` | 種族代碼 |
| `0x01` | `u8` | `clazz` | 職業代碼 |
| `0x02` | `u8` | `level` | 等級，實際出場等級由地圖單位記錄覆蓋 |
| `0x03` | `i16` | `hp_base` | 生命基礎值 |
| `0x05` | `i16` | `mp_base` | 法力基礎值 |
| `0x07` | `u8` | `move` | 移動力 |
| `0x08` | `u8[4]` | `spells` | 初始法術遮罩，bit 編號即法術編號，只涵蓋 `00`–`1F` |
| `0x0C` | `u8[6]` | `items` | 初始物品，`FF` 表示空槽 |
| `0x12` | `i16` | `ap_base` | 力量基礎值 |
| `0x14` | `i16` | `dp_base` | 耐力基礎值 |
| `0x16` | `i16` | `dx_base` | 速度基礎值 |

`hp_base`／`mp_base`／`ap_base`／`dp_base`／`dx_base` 五個都以 `MOVSX` 帶號讀入。

`items` 的前兩槽在建立單位時一律標成「裝備中」，第 3–6 槽依值是否為 `FF` 標成空或持有中，第 7、8 槽固定為空。

## 升級成長 `FRILEVUP.DAT`，11 byte

| 偏移 | 型別 | 欄位 |
| ---: | --- | --- |
| `0x00` | `u8` | `ap_min` |
| `0x01` | `u8` | `ap_max` |
| `0x02` | `u8` | `dp_min` |
| `0x03` | `u8` | `dp_max` |
| `0x04` | `u8` | `dx_min` |
| `0x05` | `u8` | `dx_max` |
| `0x06` | `u8` | `hp_min` |
| `0x07` | `u8` | `hp_max` |
| `0x08` | `u8` | `mp_min` |
| `0x09` | `u8` | `mp_max` |
| `0x0A` | `u8` | `learn_index`，指向 `GETMGTAB.DAT` 的第幾筆，`FF` 表示不學法術 |

各 `*_max` 是「最大成長值加 1」：攻略站列的每個成長範圍上限，一律等於這個 byte 減 1。

## 法術習得 `GETMGTAB.DAT`，12 byte

六組 2 byte，每組是「等級, 法術編號」，兩者皆 `FF` 表示這一組沒用到。60 筆全部符合同一個慣例：有內容的組排在前面，等級由小到大。
