# 2026-08-14 光碟盤點與 CD 音源範圍界定（票 08）

## 出發點

spec 把「CD 音源子系統的範圍」列為最大的不確定性第一名。已知的線索是三個互相矛盾的訊號：執行檔內找不到紅皮書音軌控制的指令特徵、卻有 `DEVICE REQUEST FAILED!!!` 這種裝置請求失敗訊息、還有 `%s\%s.Vid` / `%s\%s.Aud` 這種指向光碟上影音檔的路徑格式字串。若音樂根本不由主程式播放，重建範圍會小一大塊。

## 從錯誤訊息反查

先在 Ghidra 搜字串，四個關鍵字串各只有一個參照者：

- `DEVICE REQUEST FAILED!!!` → `0x3bb7d`
- `%s\%s.Vid` / `%s\%s.Aud` → `0x30f40`
- `Check your CDROM please!!!` → `0x29220`

`0x29220` 一看就是 `main`：開頭檢查 `DISK.NO`、結尾印 `Thank you for playing Flame Dragon Plus!!`，中間是 handler dispatch 迴圈。

`0x3bb7d` 是整件事的關鍵。decompiler 的輸出幾乎沒用（一堆 `_DAT_00069xxx = 常數`、還把整段標成 unreachable），但 assembly 一讀就清楚：它填一個結構後 `PUSH 0x31` 呼叫 `0x43657`，也就是 `INT 31h`。AX=0x0300 是 DPMI 的 Simulate Real Mode Interrupt，BL=0x2F 是要模擬的中斷編號，ES:EDI 指向 real-mode call structure。再對照 RMCS 的欄位位移（EAX 在 +0x1C、ECX 在 +0x18、ES 在 +0x22），寫進 EAX 的值是 `0x1510`——MSCDEX Send Device Driver Request。

**「找不到紅皮書音軌控制的指令特徵」的原因就在這裡：程式跑在保護模式，`0x2F` 是寫進暫存器結構的資料位元組，不是指令運算元，任何以指令為目標的掃描都找不到它。** 這個教訓對後面幾張票都適用——DOS/4G 程式的所有 BIOS/DOS 中斷都會長這樣。

接著把 `0x3bb7d` 的五個呼叫端逐一讀完，命令碼直接寫死在 request header 的第 3 個 byte：`0x84` Play Audio、`0x85` Stop Audio、三個 `0x03` IOCTL Input（control block `0x0A` 音樂碟資訊、`0x0B` 音軌資訊、`0x06` 裝置狀態）。到這裡結論已經確定：**音樂由主程式自己播，屬於重建範圍。**

## 影片是外部程式

`0x30f40` 反而是相反的答案。它 `sprintf` 出三條路徑後呼叫 `0x435a2`，而 `0x435a2` 只是把三個引數轉手給 `0x4e1e9`——典型的 `spawnv(mode, path, argv)` 包裝。第一條路徑是 `%s\fd.exe`（原本以為 `0x61ed4` 是別的字串，直接讀記憶體才看到），光碟上確實有一個 125 KB 的 16-bit MZ 檔 `FD.EXE`。所以影片播放不在重建範圍內，只要保留 `spawnv` 呼叫。

## 走錯的一段：自己寫 ISO9660 parser

盤點光碟時第一版是自己寫 MODE1/2352 的 raw image parser，直接解 ISO9660 目錄樹。跑得出正確結果，但這是自找的維護負擔——使用者要求改成用 DOSBox-X `imgmount` 掛載、把工作目錄 `mount` 成另一台磁碟機、在 DOS 內 `COPY e:\*.* f:\` 把檔案複製出來再分析。改完之後全部 SHA-256 與自寫 parser 的結果一致，等於順手驗證了兩條路徑，但保留下來的只有 DOSBox-X 那條。

自寫 parser 的版本還踩到一個蠢 bug：`read_range` 讀到映像結尾時 `read()` 回傳空 bytes，`while len(out) < length` 就永遠跑不完，整個指令卡住被移到背景。教訓是所有讀檔迴圈都要有短讀保護。

## 換片邏輯與章節分界

`0x30cc0` 是換片檢查。它組出 `%s\Pack.vfs` 後 `access`，存在就從那個 VFS 容器裡讀 `Pass.Dat`，取首字元減 `'0'` 當光碟編號。實際把兩片的 `PACK.VFS` 拆開來看，`PASS.DAT` 分別是 `1\r\n` 與 `2\r\n`，3 個 byte，兩片 `PACK.VFS` 的差異就只有這裡（檔案大小差 654 byte）。

判斷式是章節索引跟 `0x12` 比，也就是 30 章從第 18 章（0-based）起換第 2 片。

順帶挖到 `0x304a0` 的 60 byte 常數表，索引 `章節*2 + slot`，值加 1 就是音軌編號。這張表自己會驗證自己：章節 0–17 用到的最大音軌是 19，光碟 1 有 21 條；章節 18–29 最大是 14，光碟 2 有 15 條。兩邊都剛好不超出，等於獨立確認了「18 章分界」與「30 章」兩件事。

slot 的語意沒有下定論。五個呼叫端四個傳 0，唯一傳 1 的在戰鬥回合流程 `0x1e3f0` 裡，而且同一個 function 稍後又用 0 呼叫一次。猜得出是戰鬥曲/常態曲，但那是 function 命名那張票的事，知識庫只寫下可驗證的事實。

## 剩下的觀察

`0x3bade`–`0x3c891` 這段 3,508 byte 裡 Ghidra 只定位出 15 個 function、約 1,714 byte。用 `disassemble_bytes` 的 dry run 掃過幾個空隙，裡面都是結構完全一樣但沒有任何呼叫端的 MSCDEX 包裝（IOCTL control block `0x01`、`0x04`、`0x0C`）。沒有動手建 function——那是 Ghidra 基準修復與 orphan code 那兩張票的工作，這裡只把規模估進知識庫（整個子系統 30–35 個 function）。

光碟 2 上的 `PACK1.VFS` 有 139 MB，開頭卻是重複的假 `XXX.ARJ` / `ARCHIVE.RAR` 記錄，不是 VFS 容器，執行檔裡也沒有任何字串或路徑參照它。當成填充檔記錄，不再追。
