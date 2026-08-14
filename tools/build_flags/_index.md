# build_flags — 反推建置旗標組

從 `FDPS.LE` 反推出當年的工具鏈版本、`wcc386` 旗標與 `wlink` 指令。結論在 [`rebuild_info/build_flags.md`](../../rebuild_info/build_flags.md)。

做法是差分：用手上這幾套 Watcom 實際編譯探針原始碼，看哪一組旗標產生的程式碼形狀與原版相同，而不是從機械碼猜。輸出全部落在 `workspace/build_flags/`。

## 腳本

| 檔案 | 用途 |
| --- | --- |
| `parse_le_header.py` | 逐欄印出 LE header、object table 與 resident name，並算出初始 ESP 與 object 尾端的關係。吃 `.LE` 或帶 MZ stub 的 `.EXE` |
| `probe_matrix.py` | 用一組旗標矩陣編譯 `probe.c` 並反組譯，比較框架形狀、呼叫慣例、資料落點、浮點模型 |
| `version_sweep.py` | 同一組旗標跑遍 `WATCOM_9.5_series` 與 `WATCOM_10_series` 底下每個有 NT 版編譯器的安裝，找出版本間會分歧的程式碼形狀 |
| `lib_bytematch.py` | 拿 `FDPS.LE` 裡沒有重定位的位元組串去搜每個版本的 `LIB386`，判定連的是哪個 lib 變體（`3S` 還是 `3R`）與哪一版 |
| `dosbox_link.py` | 在 DOSBox-X 裡用 10.0a 的 DOS 版 `wlink` 連結探針，比對 MZ stub、object 佈局、堆疊大小與模擬器是否被連進去 |
| `link_defaults.py` | 分別在有／沒有 `option stack` 與 `name` 的情況下各連一次，量出 wlink 的預設堆疊大小，以及 LE 的 resident name 究竟取自輸出檔名還是第一個 `.obj`。自帶原始碼與編譯，不依賴其他腳本的產物 |
| `verify_flags.py` | **回歸閘**：用定案的旗標組編譯全部探針，逐項比對 12 個原版特徵，全過才回 0 |

## 探針原始碼

| 檔案 | 針對的問題 |
| --- | --- |
| `probe.c` | 框架形狀、引數傳遞、資料落點、浮點呼叫形式、大型框架 |
| `probe_switch.c` | switch 的跳躍表位置與分派指令形狀 |
| `shorts.c` | 16-bit 載入的處理方式，分辨 `-4s` 與 `-5s` |
| `localinit.c` | const 物件與區域陣列初值影像的落點，分辨 `-mf` 與 `-ms` |

## 注意

- `WDISASM` 會把任何開頭是 `-` 的引數當成選項，而本 repo 的路徑含 `-`。所有腳本都把工作目錄切到輸出資料夾再傳相對檔名。
- NT 版 `WLINK.EXE` 在此機器上會無回應（即使關掉 stdin），連結一律走 DOSBox-X。
- DOS 版 `wlink` 開不了超過 8.3 的 `.lnk` 檔名，所有變體標籤都要控制在 8 個字元內。
- 連結產物的新舊靠 mtime 判定而非只靠刪除：Windows 上偶爾會有掃描程序抓住剛寫出的 EXE 導致刪不掉，而 DOSBox 寫出的時間戳只有 2 秒精度且向下取整，比較時要留寬容值。
- 編譯用 NT 版 `wcc386` 是刻意的：實測與 DOS 版產生相同的機械碼，但不必等模擬器啟動。
