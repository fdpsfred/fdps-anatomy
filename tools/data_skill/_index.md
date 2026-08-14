# data_skill — 遊戲資料查詢 skill 的資料集建置

把 `MISC.VFS` 的六張資料表與知識庫的名稱合成一份 JSON，給 `.claude/skills/fdps-data/` 這個查詢 skill 用。輸出進版控，因此不寫在 `workspace/` 而是寫在 skill 自己的資料夾裡。

```
python tools/data_skill/build.py fdps_game_files/MISC.VFS .claude/skills/fdps-data/fdps_data.json
```

## 數值與名稱的分工

數值一律從 `MISC.VFS` 的 record 現解，不從知識庫抄。名稱來自 [`assets/`](../../assets/_index.md) 各正典檔與 [`chapters/_index.md`](../../chapters/_index.md) 的表格——那裡是攻略站名稱的轉錄擁有者，攻略原文本身不解析（[ADR-0006](../../docs/adr/0006-guide-as-mirrored-text-not-parsed-data.md)）。

因此建置需要讀知識庫的 markdown 表，而讀進來的每一列都會拿它的數值欄位去對同編號的 record，對不上就中止。這同時擋掉兩件事：知識庫的數值漂移，以及表格改版後解析錯位把名稱掛到錯的記錄上。

攻略站與資料檔已查明的歧異以常數寫在腳本裡，隨資料集一起輸出，查詢時會印在該筆下面。這份清單與 [`guide_offsets/crosscheck.py`](../guide_offsets/_index.md) 的 `ACCEPTED` 是同一組事實的兩份副本：那支腳本比對的欄位一旦冒出新的不一致就會失敗，所以在**它涵蓋的欄位範圍內**這份清單不會遺漏；它沒有比對的欄位（物品類型、使用對象等只存在於 record 的欄位）則沒有這層保護，往 `ACCEPTED` 加一筆時要一併加到這裡。

建置本身能擋的是清單過期：每一筆歧異的資料檔那一側都會回頭對 record 驗證，值改了就中止，不會留下一個掛錯數字的「攻略站寫…」提示。

## 只收有把握的欄位

人物的出場屬性只給 `assets/characters.md` 列出的那十二個索引，其餘索引的出場欄位一律留空。那些位置的 byte 不是空的，但它們是重複的樣板列，掛上索引就變成看似合理的假資料。

同理，沒有解讀出對照表的代碼欄位（物品類型、使用效果代碼等）只輸出原始值，不附推測的名稱。
