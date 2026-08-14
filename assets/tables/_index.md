# assets/tables — record 的 struct 定義

放 `assets/` 各正典檔所描述的資料表，其 record 在記憶體與檔案中的 struct 定義：欄位順序、型別、寬度、對齊。

數值本身不寫在這裡，一律引用上層的正典檔——這裡回答「一筆 record 長什麼樣」，上層回答「裡面的數字是多少」。承載這些 record 的容器格式（VFS 之類）則屬於 `resource_info/`。

一個 struct 一檔，檔名對應上層的正典檔名。

承載這些 record 的九個 `.DAT` 成員在容器中的位置、大小與筆數，由 [`resource_info/data_tables.md`](../../resource_info/data_tables.md) 擁有。

| 文件 | 內容 |
| --- | --- |
| [`items.md`](items.md) | `ITEM.DAT` 的 23 byte record |
| [`spells.md`](spells.md) | `MAGICDAT.DAT` 的 7 byte record，以及執行期的 40 bit 法術遮罩 |
| [`characters.md`](characters.md) | `FRIAPRDA.DAT`／`FRILEVUP.DAT`／`GETMGTAB.DAT` 三張表的 record |
| [`classes.md`](classes.md) | `PROMAP.DAT` 的 10 byte record |
