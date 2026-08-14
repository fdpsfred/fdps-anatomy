# saf_decode — SAF 動畫解碼

把 `.SAF` 的四層結構（frame、tilemap、tile、音效）全部解出來、驗證解碼結果自洽，並把動畫算成看得到的 PNG、音效寫成聽得到的 WAV。格式結論記在 [`resource_info/saf.md`](../../resource_info/saf.md)。

## `saf_decode.py`

四個子命令：

```
python tools/saf_decode/saf_decode.py list   <saf>
python tools/saf_decode/saf_decode.py verify <輸出目錄> <saf 或目錄>...
python tools/saf_decode/saf_decode.py dump   <輸出目錄> <調色盤.pal> <saf 或目錄>...
python tools/saf_decode/saf_decode.py report <manifest json>
```

`list` 印檔頭與四個 section 的項目數、大小和起點。

`verify` 收下任意多個來源（檔案或目錄，目錄會遞迴找 `.saf`），把每個檔的四層全部解開並逐項驗證，寫一份 `manifest.json`。它不算圖，所以跑全部 525 個檔只要二十秒左右。

`dump` 跑一樣完整的解碼與驗證，但不寫 manifest，改成把每個檔算成一張 filmstrip PNG——一格一張 320×200 的螢幕，依序排成格狀——並把每段內嵌音效寫成 `<檔名>-<編號>.wav`。算圖是逐像素的純 Python，所以這個命令是給抽樣看的，不要拿去跑整個資料夾。

`report` 把 manifest 印成知識庫用的 Markdown 表。

## 驗證的強度

解碼不是「讀得到就算過」：每一層都當成硬條件驗證，任何一項對不上就中止並指出是哪個檔的哪一項。四個 section 首尾相接、項目之間沒有間隙、每張表的偏移嚴格遞增；frame 的長度要剛好等於 `10 + 13 × layer 數`；每個索引都要落在被引用那個 section 的範圍內；每個 tile 的每一列剛好蓋滿 cell 寬、剛好走完 cell 高、串流剛好在下一個 tile 的偏移處用盡；音效的長度要剛好等於宣告的取樣數加 8。放水的話產出的就是看起來像動畫的雜訊。

## 座標與調色盤

filmstrip 的每一格畫的是螢幕看得到的 320×200：播放器把動畫原點設在背景緩衝的 `(24, 24)`，再從那個原點取 320×200 上螢幕，所以 layer 的 x、y 直接就是螢幕座標，超出去的部分裁掉。

調色盤不在 `.SAF` 內，得由呼叫端指定 `MISC.VFS` 裡的某個 `.PAL`；套錯的話形狀正確但顏色全錯。戰鬥相關的（`BACK*`、`STAND*`、`ACT*`、`MAGIC*`）要 `FIGHT.PAL`。

## 執行

先跑 [`vfs_dump`](../vfs_dump/_index.md) 把容器解開，再：

```
python tools/saf_decode/saf_decode.py verify workspace/saf_decode workspace/vfs_dump
python tools/saf_decode/saf_decode.py report workspace/saf_decode/manifest.json > workspace/saf_decode/report.md
python tools/saf_decode/saf_decode.py dump workspace/saf_decode/battle workspace/vfs_dump/MISC/FIGHT.PAL workspace/vfs_dump/FIGACT/ACT000.SAF
```
