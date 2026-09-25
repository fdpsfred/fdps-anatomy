# 商店 record

`FIELD.VFS` 的 `SHOPnn.DAT`，一檔 36 byte，`nn` 是章節索引。貨品見 [`assets/shops.md`](../shops.md)，容器見 [`resource_info/vfs.md`](../../resource_info/vfs.md)。

| 偏移 | 型別 | 欄位 | 內容 |
| ---: | --- | --- | --- |
| `0x00` | `u8[12]` | 道具店 | 物品編號，`0xFF` 為空位 |
| `0x0C` | `u8[12]` | 武器店 | 同上 |
| `0x18` | `u8[12]` | 秘密商店 | 同上 |

沒有 header、沒有筆數欄位，整個檔就是這三列。`fdps_load_field_chapter_resources`（`0x31540`，`src/rsrc.c`）以 `Shop%02d.dat` 與當下章節索引組名載入，整塊放在 `data_fdps_shop_stock_table_ptr`；`fdps_shop_collect_stock_items`（`0x31700`，`src/shop.c`）以「列號 × 12 + 格」取值。

- 每列固定跑滿十二格，`0xFF` 是**空位**而不是列尾，空位之後還會有貨（`SHOP01.DAT` 的武器列是 `02 71 FF FF 72 73 FF …`）。
- 物品編號以無號 byte 取值，`0x80` 以上是正常的貨。

兩點照直覺寫都會錯，見 [`rebuild_info/pitfalls.md`](../../rebuild_info/pitfalls.md)。
