# MISC.VFS 的資料表檔

`MISC.VFS` 裡有九個 `.DAT` 成員承載遊戲的數值資料。它們不是各自獨立的格式：`FDPS.LE` 在 `0x18930` 一次把九個讀進記憶體、各存一個全域指標，每張表配一支形如 `base + index * stride` 的取值函式，沒有任何 header、magic 或筆數欄位——record 從成員的第一個 byte 開始連續排列，筆數等於成員大小除以 record 大小。

容器本身的格式見 [`vfs.md`](vfs.md)。record 的欄位佈局屬於 [`assets/tables/`](../assets/tables/_index.md)，數值本身屬於 [`assets/`](../assets/_index.md)。

## 九張表

`stride` 取自 `FDPS.LE` 取值函式裡的 `IMUL`，不是從檔案大小反推的。

| 成員 | 大小 | stride | 筆數 | 取值函式 | 索引 | 正典 |
| --- | ---: | ---: | ---: | --- | --- | --- |
| `FRIAPRDA.DAT` | 1,440 | 24 | 60 | `0x18ab0` | 肖像編號 | [`assets/characters.md`](../assets/characters.md) |
| `FRILEVUP.DAT` | 660 | 11 | 60 | `0x18ae0` | 肖像編號 | [`assets/characters.md`](../assets/characters.md) |
| `GETMGTAB.DAT` | 720 | 12 | 60 | `0x18c00` | `FRILEVUP.DAT` 的習得索引 | [`assets/characters.md`](../assets/characters.md) |
| `ITEM.DAT` | 5,773 | 23 | 251 | `0x18b40` | 物品編號 | [`assets/items.md`](../assets/items.md) |
| `MAGICDAT.DAT` | 280 | 7 | 40 | `0x18bd0` | 法術編號 | [`assets/spells.md`](../assets/spells.md) |
| `PROMAP.DAT` | 410 | 10 | 41 | `0x18b70` | 職業代碼 + 1 | [`assets/classes.md`](../assets/classes.md) |
| `ENEMYDAT.DAT` | 910 | 10 | 91 | `0x18b10` | 肖像編號減 60 | [`assets/enemies.md`](../assets/enemies.md) |
| `PROEQU.DAT` | 216 | 6 | 36 | `0x18ba0` | 職業代碼 | [`assets/classes.md`](../assets/classes.md) |
| `RANKUP.DAT` | 108 | 12 | 9 | `0x18c30` | 角色編號（單位記錄 `+0x08`） | [`assets/characters.md`](../assets/characters.md) |

`ITEM.DAT` 的 251 筆裡只有前 226 筆有內容；`PROMAP.DAT` 的第 0 筆是預設值而非職業 `00`。兩者的細節在各自的正典檔。

另有一組不在 `MISC.VFS`、也不在啟動時載入的數值表：村莊商店的貨品 `SHOPnn.DAT`，放在 `FIELD.VFS`，每次進村莊才依章節索引載入，佈局見 [`assets/tables/shops.md`](../assets/tables/shops.md)，內容見 [`assets/shops.md`](../assets/shops.md)。

## 攻略站給的偏移對得上，職業表除外

攻略站 `modify2` 頁把六張表的位置寫成 `MISC.VFS` 的絕對 byte 偏移。硬碟安裝版的 `MISC.VFS`（27,398,923 byte）拿去對，其中五個**精確落在對應成員的第一個 byte**：

| 表 | 攻略站偏移 | 落點 |
| --- | ---: | --- |
| 人物出場屬性 | `0x7892F8` | `FRIAPRDA.DAT` 起點 |
| 升級屬性 | `0x789898` | `FRILEVUP.DAT` 起點 |
| 法術習得 | `0x79C427` | `GETMGTAB.DAT` 起點 |
| 物品功效 | `0x7A6234` | `ITEM.DAT` 起點 |
| 法術功效 | `0x96DF9E` | `MAGICDAT.DAT` 起點 |
| 職業相關 | `0x19D513A` | `PLYPHASE.SAF` 中段 |

職業表的偏移比這份的 `PROMAP.DAT` + 10 少了 52,184 byte，它指的是另一個發行版本的容器：前五張表精確吻合，表示那份容器的前 9.9 MB 與這份一致，差異全在 `MAGICDAT.DAT` 之後的動畫成員上。專案手上的三份都不是它——硬碟安裝版與光碟資料軌上的那份 SHA-256 相同，兩片光碟 `PACK.VFS` 內封的那份是 27,392,799 byte、`PROMAP.DAT` 在 `0x19E051C`。

職業表在這份容器裡的位置是 `PROMAP.DAT` + 10，依攻略站列出的職業資料內容比對確定，容器內沒有第二個位置符合。
