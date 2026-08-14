# guide_offsets — 攻略偏移歸屬與資料表交叉比對

把攻略站 `modify2` 給的六組資料表偏移對回 `MISC.VFS` 的成員，解出各表，再與攻略站另外三頁的人類可讀數值逐筆比對。結論記在 [`assets/`](../../assets/_index.md) 各正典檔與 [`resource_info/data_tables.md`](../../resource_info/data_tables.md)。

## `decode_tables.py`

```
python tools/guide_offsets/decode_tables.py <MISC.VFS 路徑> <輸出目錄>
```

不信任攻略站給的絕對偏移：表的位置一律從容器自己的 entry table 查成員名稱取得，攻略偏移只拿來報告它落在哪個成員、與該成員起點差多少。成員大小不是 record 大小的整數倍就中止——這是 record 大小判斷錯誤時唯一會冒出來的訊號。

輸出 `tables.json`（結構化，供比對用）與 `report.md`（六張表的完整可讀傾印）。

## `crosscheck.py`

```
python tools/guide_offsets/crosscheck.py <tables.json> <輸出 markdown>
```

攻略站的期望值以人手轉錄成腳本內的字面常數，不解析攻略原文（[ADR-0006](../../docs/adr/0006-guide-as-mirrored-text-not-parsed-data.md)）。每一組常數的註解寫明出自哪一頁哪一欄。

已查明原因的不一致列在 `ACCEPTED`；出現任何不在該表內的不一致就以非零狀態結束。要新增例外必須同時寫下原因，這樣「還沒查」與「查過了」不會混在一起。
