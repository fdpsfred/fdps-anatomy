# Data emit — 全域資料怎麼變成 C 定義

**驗證對象**：`src/` 裡每個遊戲全域的**定義**——它的型別、初值、它在 translation unit 裡的位置，以及它必須與哪個鄰居相鄰。一個全域落在哪個檔由 [`code_layout.md`](code_layout.md) 擁有，它的 `extern` 與說明由擁有者的 `.h` 擁有，function 怎麼 emit 由 [`emit_pipeline.md`](emit_pipeline.md) 擁有，本檔只回答「定義寫成什麼樣」。腳本在 [`tools/data_emit/`](../tools/data_emit/_index.md)。

## 工作清單就是連結器的抱怨

單元測試映像連結兩次（[`emit_pipeline.md`](emit_pipeline.md)「資料還沒 emit 之前怎麼連結」），第一次不帶 stub，報出來的未定義符號裡每一個遊戲全域都是還沒有定義的。定義落地之後下一次建置就不會再報它，所以清單自己縮短，「做完」的判準是清單空掉，不是某個計數到達某個數字——計數會與程式碼漂移，連結器的抱怨不會。

## 內容：原版映像的初始 byte，沒有選擇

定義存的必須是原版映像在該位址的初始 byte。映像的初值就是遊戲啟動那一刻的狀態，任何其他值都會讓重建版從不同的狀態出發：`data_fdps_ui_palette_cycle_phase` 的 15 若寫成 0，介面的調色盤波形從此永遠差半個週期，而且沒有任何程式路徑會把它拉回來。

| 映像內容 | 定義 |
| --- | --- |
| 全為 0 | tentative 定義（`int x;`），除非有下一節的佈局約束 |
| 非 0 | 帶初值的定義，值照型別的讀法寫：有號量寫有號十進位、旗標與遮罩寫十六進位 |
| 重定位過的指標 | 寫成它指向的符號名（章節四張 dispatch 表的每一格都是 function 名） |

型別照擁有者 `.h` 的 `extern` 一字不差，含 `volatile` 與元素數；定義所在的 `.c` 一律 include 自己的 `.h`，讓編譯器看到宣告與定義並排比對。`.h` 本身若與 assembly 矛盾（存取寬度、`MOVSX`／`MOVZX`、比較用 `JL` 還是 `JB`）就改 `.h`，不在定義那邊將就。程式庫型別（`union REGS`、`struct SREGS`）照標頭的拼法，不照 Ghidra 記的 byte 陣列。

## 工具鏈怎麼擺全域（實測）

量測腳本是 `tools/data_emit/layout_probe.py`，以專案旗標編兩個 unit 並讀 wlink map：

| 寫法 | 落點與順序 |
| --- | --- |
| 帶初值（含 `= 0`、`= { 0 }`） | `_DATA`，**同一個 `.c` 內照原始碼順序**，每個物件按自己的自然對齊擺：`char` 1、`short` 2、`int` 與指標 4、陣列照元素。兩個 200 byte 的 `char` 陣列接一個 `int` 恰好相鄰；一個 `char` 後面接 `int` 會留三個填充的 0 |
| 不帶初值（tentative） | `_BSS`，順序由工具鏈決定，**不是**原始碼順序（實測四個 tentative 被排成 1、2、4、3） |
| `#pragma pack(1)` | 對全域無效。不對齊的佈局（奇數位址上的 `short`、緊跟在 byte 後的 `int`）用分開的全域寫不出來 |
| 兩個 `.c` | `_DATA` 照連結順序接起來 |

所以「兩個符號必須相鄰」只有一種寫法：兩者都帶初值、落在同一個 `.c`、在原始碼裡前後緊接，而且對齊允許。tentative 定義的相鄰永遠不能依賴（[`pitfalls.md`](pitfalls.md) 的 B 類）。

## 定義在 `.c` 裡的位置就是佈局

`tools/data_emit/land.py` 把一個檔擁有的全部定義寫成緊接在 `#include` 之後的一個區塊，以兩行標記註解包起來：帶初值的在前、照**原版映像的位址順序**，零值的在後。區塊放在所有 function 之前，function 內的 `static` 初值不會插進它中間。

位址順序不是為了好看。依上一節，同一個檔內帶初值的定義照原始碼順序進 `_DATA`，因此照原版位址排序就是在重建版裡重現原版的相鄰關係——只要兩者屬於同一個檔、都帶初值。

## 佈局約束

一個符號只在有**指令層級的證據**顯示某個讀寫經由鄰居的位址碰到它（索引超過表尾、游標跑過一個陣列落進下一個、跨數個符號的區塊複製、折疊基底落在前一個符號裡）時才帶約束。只是剛好擺在隔壁、沒有程式碼跨過邊界的，沒有約束。

| 約束 | 意思 | 寫法 |
| --- | --- | --- |
| `follows: X` | 這個符號必須從 X 結束的地方開始 | 兩者都帶初值、同一個 `.c`，`land.py` 依位址把 X 排在它正前方 |
| `zero_guard_before` | 這個符號前面 4 byte 必須是重建版自己擁有、初值 0、沒有人寫的儲存（`-1` 索引會讀到） | 定義本身以一個帶初值的 `static` 0 dword 開頭，緊接在符號前 |

兩個必須相鄰的符號若被路由到不同的檔，擁有權跟著佈局走：`tools/code_emit/build_routing.py` 的 `DATA_OVERRIDES` 記下理由，`extern` 連同說明搬到新擁有者的 `.h`。

## 閘門：與原版逐 byte 比對

build gate 的 `emittest` 目標帶一個測試套件 `data_emit.check`（`tools/data_emit/check_data.py`）。它不讀 C 原始碼，讀的是連結結果：從 wlink map 取每個已落地符號在 `EMITTEST.EXE` 的位址，讀出 byte，與原版 `FDPS.LE` 在 Ghidra 位址的 byte 比對。

| 比對項 | 判定 |
| --- | --- |
| 一般 byte | 逐 byte 相等；原版的零填充區讀成 0，所以一個原版其實有初值的全域無法被當成零值蒙混過去 |
| 重定位過的指標 | 兩邊都解析成指向的符號名，名字相等（原版經 Ghidra 快照、重建版經 map）；一邊有重定位一邊沒有就是不等 |
| 定義所在的 object | 必須是 routing 指定的那個 `.c`，不能是 stub 模組 |
| `follows` | 位址恰好等於前一個符號的位址加它的大小 |
| `zero_guard_before` | 前 4 byte 為 0，而且沒有任何公開符號從那 4 byte 開始 |

它看不見的：存同樣 byte 的兩種型別（有號或無號）——那由編譯器對 `.h` 的比對與判定時讀 assembly 負責；以及沒有人記下來的佈局依賴。

## 中斷復原的界線與足跡

照 [`emit_pipeline.md`](emit_pipeline.md) 的「中斷復原」做，差別只在界線與足跡。

**界線**是這條 pipeline 自己各段會寫的路徑，開跑第一段只清這些，其他路徑髒了就停下來報告：

```
src/  tests/  tools/data_emit/data/  ghidra_snapshot/
tools/code_emit/build_routing.py  tools/code_emit/data/routing.json
tools/code_emit/data/routing.md  rebuild_info/code_layout.md
```

`tests/` 在裡面，是因為修復段可能要改一個斷言了 stub 零值的測試；路由三個檔與 `code_layout.md` 在裡面，是因為改變擁有權的那一段會動它們。知識庫頁、devlog、索引由各自的段落當場以 pathspec commit 掉，不留在工作區。

**足跡**是 `workspace/data_emit/in_flight.json`，不是追蹤中的狀態檔。本工作沒有狀態檔——清單每次由連結器重生——所以足跡另外放：落地段在碰 `src/` 之前寫下這一批的符號，commit 或丟棄之後刪掉。它在 `workspace/` 底下，git 看不到它，所以不需要擠進界線，也不會被收拾段當成殘骸清掉；下一輪開跑時讀得到它，就代表上一輪死在那一批落地中。

判定段不需要足跡：判定 agent 除了自己的判定檔之外什麼都不寫，死掉只會留下一個通不過驗證的判定檔，而那正好讓該符號回到清單上。所以「跑到一半被中斷」只可能發生在落地段，收尾報告據此把「上一輪中斷在哪一批」與「這一輪根本沒輪到」分開列。

## 落地的批次

一次 gate 要二三十分鐘（建置約 7 分鐘，其餘是測試）。所以先把全部就緒的判定一次落地、一次 gate；過了就一個 commit。紅燈先交給修復段（改錯的判定、或改斷言了 stub 值的測試），修不好就整批丟棄，改成一個檔一個檔重來，讓一個錯的判定只拖累它自己的檔。
