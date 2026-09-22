# 遊戲文字：`FDETXTnn.TXT` 與 `FDETXT.FON`

`FIELD.VFS` 裡的文字資源。容器本身見 [`vfs.md`](vfs.md)。

## `FDETXTnn.TXT`

一個檔是一個文字區塊：

- 開頭是一張 signed 16-bit 的 byte offset 表，筆數 = 第一個 offset / 2，offset 相對於區塊起點
- 接著是各筆的 signed 16-bit token 串，以 -1 結尾

| token | 意義 |
| --- | --- |
| `>= 0` | 字模索引，直接索引 `FDETXT.FON` |
| -1 | 結尾 |
| -2 | 換行 |
| -3 | 換頁 |
| -4／-5 | 代入全域文字 |
| -6 | 數字 |
| -0x11／-0x12 | 後接一個 operand（角色 id／單位索引） |

檔名的編號是章節索引加 1，也就是玩家看到的章號；`FDETXT00.TXT` 是全域文字，以固定檔名載入。重建時會踩的雷見 [`rebuild_info/pitfalls.md`](../rebuild_info/pitfalls.md)。

### `FDETXT00.TXT` 的佈局

| 筆 | 內容 |
| --- | --- |
| `0x01`..`0x0f` | 我方角色名 |
| `0x3c`..`0x81` | 敵方與 NPC 單位名 |
| `0x97`..`0x9d` | 七個種族名：人類、妖鬼、魔族、機械、獸人、龍族、其他，對應種族代碼 0..6 |
| `0x9e`..`0xa0` | 空字串 |
| `0xa1` 起 | 職業名，`0xa1` + 職業代碼 |

出貨的 `FRIAPRDA.DAT` 與 `ENEMYDAT.DAT` 種族代碼只有 0..6，職業代碼最大 `0x26`。

## `FDETXT.FON`

無檔頭的字模表：1792 個 16×16 1bpp 字模，每個 32 byte，token 直接索引，不是 Big5。其中 1354 個與前作 FD2 `FDOTHER.DAT[4]` 的字模逐 byte 相同，可直接沿用前作的字對照表，其餘以倚天 `STDFONT.15` 像素比對。
