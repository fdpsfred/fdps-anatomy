# kbd_probe — DOS/4GW 之下手塞鍵盤環形緩衝區的可見性

量「直接改寫 BIOS 鍵盤環形緩衝區（`0x41a`／`0x41c`）之後，鍵盤查詢什麼時候看得到那些鍵」。結論與它對測試的要求由 [`rebuild_info/emit_pipeline.md`](../../rebuild_info/emit_pipeline.md) 的測試一節擁有，本檔只講腳本。

| 檔案 | 用途 |
| --- | --- |
| `probe.c` | DOS/4GW 探針。每一輪先等到 timer tick 邊界，依 `arm` 決定塞鍵之前要不要先對空的環做一次查詢（`INT 21h` AH=0Bh 或 `INT 16h` AH=01h），塞入兩個鍵，再記下第一次查詢看不看得到（`first`）、看不到的話還要再查幾次（`more`）、以及那段期間 `0x46c` 前進了幾次（`dtick`） |
| `probe.py` | 用重建工具鏈（沿用 `tools/fdps_build/build_min.py` 的機制，不修改它）在 DOSBox-X 裡編譯並執行探針 N 次，每次的紀錄寫到 `workspace/kbd_probe/logs/` |

## 跑法

```
python tools/kbd_probe/probe.py all 10     # 編譯後跑 10 次
python tools/kbd_probe/probe.py run 10     # 只跑
```

判讀：`arm` 為 1、2 的輪次（塞鍵之前同一個 tick 內有過空查詢）`first=0` 且 `dtick=1`；`arm` 為 0、3 的輪次 `first=1`。
