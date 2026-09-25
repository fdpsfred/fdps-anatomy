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

`FDETXT00.TXT` 的 555 條分成單位、種族、職業、物品、法術五張名稱表與一段系統訊息，各表以「編號 + 表的起點」取條目。各分區的範圍、公式、全文與讀取端由 [`assets/text/global_text.md`](../assets/text/global_text.md) 擁有；地圖 30 以上的額外場景區塊 `FDETXT31`–`FDETXT65` 的內容見 [`assets/text/scene_text.md`](../assets/text/scene_text.md)。

永遠不會顯示的條目——沒有讀取端的條目、沒有腳本切過去的整個區塊——由 [`cut_content/story.md`](../cut_content/story.md) 逐條歸到一個條目或排除項，屬於條目的全文照錄。

出貨的 `FRIAPRDA.DAT` 與 `ENEMYDAT.DAT` 種族代碼只有 0..6，職業代碼最大 `0x26`。

## `FDETXT.FON`

無檔頭的字模表：1792 個 16×16 1bpp 字模，每個 32 byte，每列 2 byte、最高位元在左。token 直接索引，不是 Big5。每個索引代表哪個字見 [`assets/text/glyph_table.md`](../assets/text/glyph_table.md)。

- 字形都畫在上方 15 列，第 16 列在全部 1792 個字模裡都是空的，所以能與倚天 `STDFONT.15` 的 16×15 字模直接比對
- 索引 `0x020B` 與 `0x04A9` 兩格全空白、內容相同，其餘 1790 個字模各不相同
- 1354 個與前作 FD2 `FDOTHER.DAT[4]` 的字模逐 byte 相同，但其中只有 6 個索引也相同，不能拿前作的對照表按索引直接套
- 數字代碼（-6）不經對照表：它把 `sprintf` 出來的每個字元減 `'0'` 直接當字模索引，因此字模表必須把數字 0–9 的字形放在索引 0–9
