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
存檔檔案中的章節存檔格位，共 4 個，各自獨立。空格位以固定標記辨識。
_Avoid_: 存檔格、save slot、欄位

**Live-state**：
存檔檔案中記錄當下遊戲進行狀態的區段，與 slot 區分離。章節存檔採 read-modify-write，只寫入選定的 slot，不觸及 live-state。
_Avoid_: 現況、current state、即時狀態
