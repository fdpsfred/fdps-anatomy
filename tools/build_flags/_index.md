# build_flags — 反推建置旗標組

從 `FDPS.LE` 反推出當年的工具鏈版本、`wcc386` 旗標與 `wlink` 指令。結論在 [`rebuild_info/build_flags.md`](../../rebuild_info/build_flags.md)。

做法是差分：用手上這幾套 Watcom 實際編譯探針原始碼，看哪一組旗標產生的程式碼形狀與原版相同，而不是從機械碼猜。**所有 Watcom 工具一律在 DOSBox-X 裡跑 DOS 版執行檔**（原版就是這樣建出來的），host 端的 Python 只負責產生輸入、輪詢結束、解析輸出。輸出全部落在 `workspace/build_flags/`。

## 腳本

| 檔案 | 用途 |
| --- | --- |
| `parse_le_header.py` | 逐欄印出 LE header、object table 與 resident name，並算出初始 ESP 與 object 尾端的關係。吃 `.LE` 或帶 MZ stub 的 `.EXE` |
| `probe_matrix.py` | 用一組旗標矩陣編譯 `probe.c` 並反組譯，比較框架形狀、堆疊檢查、收尾形式、索引縮放與 const 落點。變體標籤是 `v01`…`vNN`（DOS 只認 8.3），`--legend` 印出對照表 |
| `version_sweep.py` | 用定案旗標跑遍每一個安裝版本的 DOS 版編譯器，列出版本間會分歧的程式碼形狀。定案旗標下每個版本都全中，所以它證明的是「旗標組不綁版本」 |
| `opt_sweep.py` | 在定案旗標之外每次多加一個旗標編 `scale.c`，找出哪個旗標會改變索引縮放形式。`--help-text` 印出 `wcc386` 自己的選項清單 |
| `ot_order.py` | 比較 `-od`／`-ot` 各種先後與縮寫組合，同時看框架、區域變數與縮放三項，找出三項全中的那一種 |
| `lib_bytematch.py` | 拿 `FDPS.LE` 裡沒有重定位的位元組串去搜每個版本的 `LIB386`，判定連的是哪個 lib 變體（`3S` 還是 `3R`）與哪一版。純檔案比對，不執行任何工具 |
| `dosbox_link.py` | 編譯並連結探針，比對 MZ stub、object 佈局、堆疊大小與模擬器是否被連進去 |
| `link_defaults.py` | 分別在有／沒有 `option stack` 與 `name` 的情況下各連一次，量出 wlink 的預設堆疊大小，以及 LE 的 resident name 究竟取自輸出檔名還是第一個 `.obj` |
| `push_form.py` | 找出什麼會讓 `wcc386` 把記憶體運算元經 EAX 中轉再推成引數。兩軸：`--flags` 在定案旗標上逐一多加一個旗標，`--versions` 用定案旗標跑遍每個安裝版本。結論是沒有任何一組產出原版那種混用 |
| `oe_probes.py` | 編譯 `oe_probes/` 的探針並逐項判定：三種 inline 關鍵字是否都是 E1009、`-od`／`-d2`／`-oe` 各組合下 callee 有沒有被展開與三種推引數形式、查表間接呼叫用哪個暫存器索引，以及 `-oe` 不展開的呼叫位置；另外直接從 `FDPS.LE` 數出遊戲 function 內每一個 `CALL [reg+disp]` 的前導形狀。全過才回 0 |
| `fn_match.py` | 整支 function 的比對：用指定的旗標組編譯真實的 `src/` 單元（與 `fn_variants.py` 的改寫版），每支 function 與出貨的 `FDPS.LE` 原始位元組逐 byte 比。原版位址與大小取自 `ghidra_snapshot/functions.txt`、名稱與所屬單元取自 `tools/code_emit/data/routing.json`、重定位欄位由 LE fixup record 遮罩（`tools/build_gate/lefixup.py`），另遮 `E8` 的 rel32、兩邊都去掉尾端對齊填充。輸出逐 function 表與統計。`--preset`（`game`／`cd`／`variants`／`order`／`claims`）或 `--sets`／`--set 標籤="旗標"`／`--units`／`--variants` 自選 |
| `fn_variants.py` | 把 `src/` 裡「原版在此有展開副本、`src/` 手寫攤開」的地方改回呼叫的五個改寫版（`palv`／`safv`／`gauv`／`chvv`／`chvw`），供 `fn_match.py` 與 `oe_threshold.py` 看 `-oe` 能不能自己長出原版的展開。改寫在執行時套在當下的 `src/` 上，找不到目標就報錯 |
| `oe_threshold.py` | 對真實單元與改寫版掃 `-oe=N`：同一單元內每一對（呼叫者, 被呼叫者），比對原版是否真的 `CALL`、重建在各個 N 是否仍 `CALL`，找出所有會隨 N 改變的配對都與原版一致的 N 區間；並列出各 N 下每個單元的 exact 數 |
| `verify_flags.py` | **回歸閘**：用定案的旗標組編譯全部探針，逐項比對 15 個原版特徵，全過才回 0。可用 `python verify_flags.py <旗標...>` 換一組旗標跑，用來證明某個旗標差異在這 15 項上分不出來 |

## 探針原始碼

檔名一律 8.3，DOS 工具看不到長檔名。

| 檔案 | 針對的問題 |
| --- | --- |
| `probe.c` | 框架形狀、引數傳遞、資料落點、浮點呼叫形式、大型框架 |
| `probesw.c` | switch 的跳躍表位置與分派指令形狀 |
| `shorts.c` | 16-bit 載入的處理方式，分辨 `-4s` 與 `-5s` |
| `locinit.c` | const 物件與區域陣列初值影像的落點，分辨 `-mf` 與 `-ms` |
| `scale.c` | 索引縮放形式（`lea` 還是 `shl`），四種來源寫法各一個 function。這是目前沒有任何安裝版本能重現原版的那一項 |
| `pusharg.c` | 引數是直接 `PUSH` 記憶體還是先 `MOV EAX` 再推。兩個 function 分別推區域變數與推傳入參數 |
| `oe_probes/kwinl.c`、`kwuinl.c`、`kwplain.c` | `_inline`／`__inline`／`inline` 在 10.0a C 是否為關鍵字 |
| `oe_probes/expand.c` | 無關鍵字的 static callee 被 `-oe` 展開與否、展開副本／自身呼叫／out-of-line 副本各自的推引數形式、查表間接呼叫帶記憶體引數與帶立即值時的索引暫存器 |
| `oe_probes/context.c` | `-oe` 在哪些呼叫位置展開：敘述、指派、`if`、`while`、當引數、`&&` 左運算元會展開；`&&`／`\|\|` 右運算元不展開；同一運算式含兩個呼叫時左邊那個展開、右邊那個留成 `CALL` |
| `oe_probes/thresh.c` | 1 到 16 條相同敘述的 callee 各被呼叫一次，量 `-oe=N` 的 N 是大小上限以及預設值落在哪裡 |

## 注意

- **不要用 `BINNT` 底下的工具做判定**。實測 NT 版與 DOS 版在這些探針上產出相同的機械碼，但判定依據要建立在與原版相同的執行環境上。DOS 版 `wcc386` 在 9.5 系列是 `BIN`、10.0 家族是 `BINB`、10.5 之後是 `BINW`，所以批次檔把三個目錄都放進 `PATH`。
- DOS 工具開不了超過 8.3 的檔名：探針原始碼、變體標籤、`.lnk` 檔名全部要壓在 8 個字元內。`wlink` 開不到 `.lnk` 時只在 `build.out` 留一行 `cannot open`，不會讓整批停下來。
- 產物的新舊靠 mtime 判定而非只靠刪除：Windows 上偶爾會有掃描程序抓住剛寫出的檔案導致刪不掉，而 DOSBox 寫出的時間戳只有 2 秒精度且向下取整，比較時要留寬容值。
- 判斷資料落在哪個段要**逐行走 `SEGMENT`／`ENDS` 區塊**，不能用「從 `_TEXT SEGMENT` 一路比對到符號」的 regex——每份反組譯都以 `_TEXT SEGMENT` 開頭，那種寫法對後面任何一段的資料都會命中。
- `wdisasm` 把「只有索引、沒有基底暫存器」的定址寫成 `+0H[eax*4]` 而不是 `[eax*4]`。抓 `lea` 縮放的 regex 少寫 `\+0H` 就會全部漏掉，看起來像「沒有任何版本產得出來」。
