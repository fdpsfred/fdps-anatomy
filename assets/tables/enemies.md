# 敵方單位 record

`ENEMYDAT.DAT` 的一筆，10 byte。數值見 [`assets/enemies.md`](../enemies.md)，檔案層面的事實見 [`resource_info/data_tables.md`](../../resource_info/data_tables.md)。對應 `src/fdpstype.h` 的 `struct fdps_enemy_data`。

| 偏移 | 型別 | 欄位 | 內容 |
| ---: | --- | --- | --- |
| `0x00` | `u8` | `race_id` | 種族代碼（0..6，名稱見 [`assets/races.md`](../races.md)） |
| `0x01` | `u8` | `class_id` | 職業代碼（見 [`assets/classes.md`](../classes.md)） |
| `0x02` | `u16` | `hp` | HP 係數 |
| `0x04` | `u8` | `mp` | MP 係數 |
| `0x05` | `u8` | `ap` | AP 係數 |
| `0x06` | `u8` | `dp` | DP 係數 |
| `0x07` | `u8` | `dx` | DX 係數 |
| `0x08` | `u8` | `mv` | 移動力，絕對值 |
| `0x09` | `u8` | `exp_reward` | 經驗值倍率，不是固定經驗值；物理攻擊與法術的算法不同，見 [`assets/enemies.md`](../enemies.md#擊殺經驗) |

係數乘上出場等級即出場值；種族、職業與 MV 是絕對值。

## 索引是肖像編號減 60

取值函式 `0x18b10` 算的是 `base + index * 10`，呼叫端傳的是地圖單位的肖像編號減 `0x3C`，不做任何界限檢查。
