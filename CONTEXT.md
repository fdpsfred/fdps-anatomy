# FDPS 逆向工程

繁體中文版 DOS 遊戲「炎龍騎士團外傳」(Flame Dragon Plus，簡稱 FDPS) 的逆向工程專案。目標是從 `FDPS.LE` 的機械碼還原出完整的 C 原始碼，並用當年的開發環境重新編譯出功能等價的執行檔。

本文件是詞彙表。決策記錄在 [`docs/adr/`](docs/adr/)，事實結論在知識庫，開發過程在 `devlog/`。

## Language

### 逆向工程

**Pool**：
每個 function 的身分歸類，共四種：`fdps`（遊戲本體）、`crt`（Watcom runtime）、`ail`（Miles 音效庫）、`binary_artifact`（編譯器產物，不對應任何原始碼）。Pool 成員在位址上互相交錯，必須逐一判定，不能用位址範圍圈定。
_Avoid_: 分類、category、群組

**三源**：
判定一個 function 時必須同時取得的三份 Ghidra 輸出——plate comment、disassembly、decompiled C。三者缺一不可，且 decompiled C 不得單獨採信。
_Avoid_: 三個來源、triple-source

**Emit**：
把一個 function 從 Ghidra 的分析結果寫成語意等價的 C 原始碼。
_Avoid_: 移植、翻譯、還原、轉換

**功能等價**：
重建版執行檔與原版在外顯行為與功能上一致的要求。不包含 binary byte 層級的一致性。
_Avoid_: 100% 復刻、byte-exact、二進位一致

**Snapshot**：
Ghidra 內部狀態匯出成純文字後進版控的形式，內容為 function 簽章、plate comment、struct 定義、label 與 global 命名。它讓「Ghidra 改了什麼」變成可 diff 的東西。
_Avoid_: 匯出、dump、備份

**知識庫**：
記錄「結論是什麼」的文件集合，採結論式寫作，禁止描述分析過程。
_Avoid_: 文件、docs、KB

**Devlog**：
記錄「怎麼走到這個結論」的敘事記錄，按時間排列，允許流水帳，重點在記下失敗與死路。與知識庫嚴格分離。
_Avoid_: 日誌、工作紀錄、journal

### 關卡

**章號**：
玩家與攻略站看到的章編號，1 起算，共第 1 章至第 30 章。`chapters/chNN.md` 的檔名用它。
_Avoid_: 關卡編號、level、關數

**章節索引**：
`FDPS.LE` 內部使用的章編號，0 起算，值域 0–29，等於章號減 1。程式內嵌的常數表（音軌表等）與全域變數一律以它為索引。引用程式側的表格時要先確認手上的數字是哪一種，兩者差 1。
_Avoid_: 章索引、chapter id、章節編號

**地圖編號**：
場景的資源編號，決定 `MAP%02d.COD`、`M%02d.DTL`、`M%02d<層>.CEL`、`ATTR%02d<層>.DAT`、`DSC%02d.DAT`、`FDETXT%02d.TXT` 這一整組檔名。0–29 與章節索引重合，31 以上是城鎮之類不對應章節的額外場景。切換由腳本指令帶一個 byte 運算元寫入，所以它比章節索引寬——**看到 30 以上的編號不代表有第 31 章**。章節專屬的分派表只有 30 筆。

各組檔案涵蓋的編號不一樣，引用時要按組確認而不是套一個通用值域：地形三組（`DTL`、`CEL`、`ATTR`）是 0–64 缺 30，腳本（`COD`／`DAT`）再缺 33，`DSC` 再缺 31，`FDETXT` 則是 0–65 連續無缺。
_Avoid_: map id、場景編號

### 資源檔

**VFS**：
FDPS 的具名檔案封裝容器，header 帶 `"VFS"` magic 與 26-byte 定長 entry（含 8.3 檔名）。取代了前作使用的 `LLLLLL` DAT 格式。
_Avoid_: archive、封裝檔、資料檔

**CEL**：
sprite 圖檔容器，前作 `FDICON.B24` 格式的直系後代，多了 magic header。像素採 RLE 4-op 編碼。
_Avoid_: 圖檔、sprite 檔

**SAF**：
多 frame 動畫容器，前作沒有對應格式。是 VFS 內數量最多的資源類型。
_Avoid_: 動畫檔、animation

### 存檔

**Slot**：
存檔檔案中的章節存檔格位。檔案裡有 4 個，各自獨立，但存讀檔畫面只選得到前 3 個，第 4 個是**前作遺留**：檔案佈局與 FD2 相同，FD2 四格都選得到，FDPS 把格數減成 3 後留下了第 4 格。空格位以固定標記辨識。
_Avoid_: 存檔格、save slot、欄位

**Live-state**：
存檔檔案中記錄當下遊戲進行狀態的區段，與 slot 區分離。章節存檔採 read-modify-write，只寫入選定的 slot，不觸及 live-state。
_Avoid_: 現況、current state、即時狀態

### 刪減與未用

下列各類之間沒有上位總稱，一條痕跡只歸一類。編譯器產物、第三方程式庫（Watcom CRT、Miles AIL 等）沒用到的 API、不可能觸發的防禦性分支不屬於任何一類。遊戲自己的模組（轉場、音訊包裝層等）裡做好卻沒被呼叫的成員不算通用程式庫，歸殘留內容。

**殘留內容**：
已經做好（資源、數值或劇本齊全到足以運作）卻沒有任何遊戲路徑能到達的內容。例：地圖 49 的索爾被擒過場、四種從未部署的敵兵。
_Avoid_: 死資料、cut content、unused data

**空殼**：
機制、欄位或 slot 存在，但沒有內容填進去。例：過場腳本能播放語音的指令沒有任何腳本使用，容器裡也沒有語音檔。與殘留內容的分界在「有沒有內容」：空殼缺的是內容，殘留內容缺的是路徑。
_Avoid_: stub、placeholder、佔位

**被封住的內容**：
內容與通往它的路徑都在，但原版的 bug 讓玩家實際上拿不到或看不到。例：1998/1/28 酒館抽獎的大獎。依功能等價，重建版照樣封住。
_Avoid_: bug 內容、鎖住的內容

**前作遺留**：
從前作 FD2 繼承、在 FDPS 裡已經沒有作用的程式或資料。它說明的是開發沿革，不代表 FDPS 規劃過又刪掉。FDPS 改寫過卻沒有啟用的，代表 FDPS 曾為它動手，歸殘留內容；原封不動繼承的才是前作遺留。
_Avoid_: 殘留、legacy
