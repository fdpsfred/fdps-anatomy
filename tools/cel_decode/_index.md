# cel_decode — CEL 圖檔解碼

把 `.CEL` 的每個 sprite 解出來、驗證解碼結果自洽，並算圖成可以直接看的 PNG。格式結論記在 [`resource_info/cel.md`](../../resource_info/cel.md)。

## `cel_decode.py`

三個子命令：

```
python tools/cel_decode/cel_decode.py list   <cel>
python tools/cel_decode/cel_decode.py dump   <輸出目錄> <調色盤.pal> <cel 或目錄>...
python tools/cel_decode/cel_decode.py report <manifest json>
```

`list` 印檔頭與每個 sprite 的偏移和大小。

`dump` 收下任意多個來源（檔案或目錄，目錄會遞迴找 `.cel`），解出每個 sprite，一個 `.CEL` 算一張把全部 sprite 排在一起的 PNG，另外寫一份 `manifest.json`。沒有任何 op 寫到的像素在 PNG 裡是全透明的，所以看得出 sprite 的輪廓，不會跟調色盤索引 0 混淆。

`report` 把 manifest 印成知識庫用的 Markdown 表。

解碼是照 `0x56a0d` 的繪製器逐列重跑，每一步都當成硬條件驗證：每列的指令剛好蓋滿檔頭宣告的寬、剛好走完宣告的高、串流剛好在下一個 sprite 的偏移處用完。任何一項對不上就中止並指出是哪個 sprite 的哪一列，因為解碼器一旦在這裡放水，產出的就是看起來像圖的雜訊。`M310.CEL` 那種 `0x0D` 為 1 的編碼另走一條解碼路徑。

調色盤不在 `.CEL` 內，得由呼叫端指定 `MISC.VFS` 裡的某個 `.PAL`；套錯的話形狀正確但顏色全錯（`STBOARD.CEL` 要 `STBOARD.PAL`，戰鬥畫面要 `FIGHT.PAL`，其餘多半是 `FDE.PAL`）。

## 執行

先跑 [`vfs_dump`](../vfs_dump/_index.md) 把容器解開，再：

```
python tools/cel_decode/cel_decode.py dump workspace/cel_decode workspace/vfs_dump/MISC/FDE.PAL fdps_game_files workspace/vfs_dump
python tools/cel_decode/cel_decode.py report workspace/cel_decode/manifest.json > workspace/cel_decode/report.md
```
