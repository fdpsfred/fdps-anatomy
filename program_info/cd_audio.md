# CD 音源與光碟相依子系統

驗證對象：`FDPS.LE` 位址 `0x3bade`–`0x3c891`（MSCDEX 存取層）、`0x30bf0`–`0x30f36`（遊戲側音樂與影片介面）、`0x29220`（`main`）。

## 結論：音樂播放屬於重建範圍

遊戲音樂是光碟紅皮書音軌，但**音軌的選曲、起播、停止、循環全部由 `FDPS.LE` 自己下命令**，不是交給外部程式。因此 CD 音源子系統必須連同重建。

執行檔內找不到 `INT 2Fh` 指令，是因為程式跑在 DOS/4G 保護模式下，透過 DPMI `INT 31h` AX=0300h（Simulate Real Mode Interrupt）發出真實模式中斷，中斷編號 `0x2F` 是寫進暫存器結構的資料位元組而不是指令運算元。

不屬於重建範圍的只有影片播放：三段過場動畫由光碟上的外部程式 `FD.EXE` 播放，`FDPS.LE` 只負責 `spawnv` 呼叫它。

## MSCDEX 存取層

所有 CD 命令共用同一個發送 function（`0x3bb7d`）。它填好 DPMI real-mode call structure 後發出 `INT 31h` AX=0300h、BL=2Fh，等同真實模式的 `INT 2Fh` AX=1510h（MSCDEX Send Device Driver Request），ES:BX 指向放在真實模式記憶體裡的 device driver request header。發送後若回傳的錯誤欄位非零，印出 `DEVICE REQUEST FAILED!!!`。

真實模式緩衝區由 `0x3bade` 以 DPMI `INT 31h` AX=0100h 配置兩塊各 0x20 個 paragraph（512 byte）：一塊放 request header，一塊放 IOCTL control block。兩塊的段位址與線性位址分別存在 `0x69e54`/`0x69de8` 與 `0x69da8`/`0x69da4`。

已定位的命令包裝：

| 位址 | request header 長度 | 命令碼 | 作用 |
| --- | ---: | --- | --- |
| `0x3bfa5` | 26 | 3（IOCTL Input），control block `0x0A` | 取得音樂碟資訊：起訖音軌編號與 leadout 位址 |
| `0x3c0c8` | 26 | 3（IOCTL Input），control block `0x0B` | 取得指定音軌的起始位址與控制位元 |
| `0x3c34f` | 26 | 3（IOCTL Input），control block `0x06` | 取得裝置狀態字 |
| `0x3c452` | 22 | `0x84` | Play Audio，帶起始磁區與長度 |
| `0x3c4a7` | 13 | `0x85` | Stop Audio |

輔助運算：`0x3bc3f` 把 24-bit 值拆成 M/S/F 三個位元組，`0x3bc78` 把 MSF 換算成 HSG 磁區位址（`(M*60+S)*75+F-150`）。

裝置狀態字存在 `0x69e20`。`0x3c6d0` 判讀其 bit 9，`0x3c6e8` 先查狀態再判讀，得到「音軌是否仍在播放」。

### DOSBox-X 的 MSCDEX 不讀未初始化的欄位

DOSBox-X 內建的 MSCDEX（`src/dos/dos_mscdex.cpp`）處理 `INT 2Fh` AX=1510h 的 IOCTL Input（命令 3）與 IOCTL Output（命令 `0x0C`）時，request header 只讀 `+2`（命令碼）與 `+0x0E`／`+0x10`（transfer address），宣告長度（`+0`）、`+0x12` 之後的欄位、以及超出 26 byte 的部分一概不讀；分派只看 transfer buffer 的第一個 byte（control block 代碼）。Read Audio Track Info（control block `0x0B`）只讀 block `+1` 的音軌號，無論成功與否都無條件寫回 block `+2`..`+6`（frame、second、minute、0、attr），查詢被拒時寫的是零。因此在 DOSBox-X 下，各 CD 請求在 header 與 control block 裡留下的未初始化堆疊位元組不影響結果，重建版的堆疊配置不同也不會造成行為差異。

## 啟動時的光碟偵測

`main`（`0x29220`）在進入遊戲迴圈前依序做三件事：

1. `access("DISK.NO", 0)`。檔案不存在就印 `Can't found file 'Disk.No' !!!` 與 `Please use install function.` 後 `exit(1)`。
2. 以 `"rt"` 開啟 `Disk.no`，連續三次 `fscanf(fp, "%s", ...)`，第三個 token 寫進全域 `0x643e8`，成為之後所有光碟路徑的前綴。安裝程式寫出的內容是 `CDROM at e:`，因此前綴為磁碟機代號。
3. 呼叫 `0x3c636`：發 `INT 2Fh` AX=1500h BX=0（MSCDEX 安裝檢查）。回傳 BX（光碟機數量）為 0 時直接回 0；否則記下第一台光碟機代號到 `0x69dfe`，配置真實模式緩衝區並查詢音樂碟資訊，狀態字等於 `0x810C` 回 2，其餘回 1。`main` 只接受回傳值 1，否則印 `Fatal error: CDROM i...` 與 `Check your CDROM please!!!` 後 `exit(1)`。

自動化建置環境要跑起遊戲，必須同時滿足：工作目錄有 `DISK.NO` 且其第三個 token 指向已掛載的光碟機、DOSBox-X 以 `imgmount -t cdrom` 掛上光碟映像、MSCDEX 介面可用。

離開遊戲迴圈後 `main` 呼叫 Stop Audio、以 `INT 10h` AX=0003h 切回文字模式，印出 `Thank you for playing Flame Dragon Plus!!`。

## 遊戲側音樂介面

- `0x30cc0`：換片檢查與起播。先 Stop Audio，再組出 `"%s\Pack.vfs"` 並 `access` 之；檔案存在時從該容器讀出 `Pass.Dat`，取首字元減 `'0'` 得到目前光碟編號。章節索引 < 18 需要光碟 1，>= 18 需要光碟 2；不符就顯示提示圖（sprite `0x222` 為換第 1 片、`0x223` 為換第 2 片）並停在迴圈直到放入正確的光碟。通過後依章節查表起播。
- `0x30bf0`：直接指定音軌起播，傳入 `-1` 表示停止。
- `0x30c50`：主迴圈每 75 次呼叫檢查一次，音軌已播完就重新起播同一首，達成循環播放。
- `0x3c85b`：起播單一音軌。流程是 Stop Audio → 查該音軌資訊 → 由 `0x3c803` 算出播放區間（下一軌起點，最後一軌則用 leadout）→ Play Audio。

相關全域：`0x60008` 為音樂開關旗標，`0x69cf4` 為目前章節索引，`0x69d54` 為目前選定的音軌索引（`-1` 表示無）。

## 章節音軌對照表

常數表位於 `0x304a0`，60 個位元組，索引為 `章節索引 * 2 + slot`。表中的位元組值加 1 才是 MSCDEX 音軌編號（音軌 1 是資料軌），加法由 `0x30cc0` 在呼叫 `0x3c85b` 前完成。**下表列的是換算後的 MSCDEX 音軌編號，不是表中的原始位元組**，直接使用即可。五個呼叫端中四個傳 slot 0，唯一傳 slot 1 的是戰鬥回合流程 `0x1e3f0`，同一個 function 稍後又以 slot 0 呼叫一次。

| 章節索引 | slot 0 | slot 1 | 光碟 |
| ---: | ---: | ---: | ---: |
| 0 | 14 | 7 | 1 |
| 1 | 14 | 7 | 1 |
| 2 | 14 | 7 | 1 |
| 3 | 14 | 7 | 1 |
| 4 | 5 | 5 | 1 |
| 5 | 5 | 5 | 1 |
| 6 | 10 | 10 | 1 |
| 7 | 15 | 19 | 1 |
| 8 | 14 | 7 | 1 |
| 9 | 7 | 5 | 1 |
| 10 | 10 | 10 | 1 |
| 11 | 15 | 15 | 1 |
| 12 | 14 | 7 | 1 |
| 13 | 14 | 7 | 1 |
| 14 | 14 | 5 | 1 |
| 15 | 14 | 7 | 1 |
| 16 | 14 | 5 | 1 |
| 17 | 10 | 10 | 1 |
| 18 | 7 | 8 | 2 |
| 19 | 6 | 8 | 2 |
| 20 | 6 | 8 | 2 |
| 21 | 6 | 8 | 2 |
| 22 | 7 | 5 | 2 |
| 23 | 7 | 8 | 2 |
| 24 | 7 | 13 | 2 |
| 25 | 2 | 13 | 2 |
| 26 | 14 | 10 | 2 |
| 27 | 5 | 11 | 2 |
| 28 | 5 | 11 | 2 |
| 29 | 14 | 13 | 2 |

表中的音軌編號都落在對應光碟實際存在的編號範圍內：章節 0–17 用到 5–19，光碟 1 的音軌編號為 2–22；章節 18–29 用到 2–14，光碟 2 為 2–16。要比對的是編號範圍而非音軌條數——條數相符不保證編號涵蓋得到。

## 影片播放外呼

`0x30f40` 收一個片名字串，組出三條路徑後以 `spawnv(P_WAIT, path, argv)` 執行光碟上的 `FD.EXE`：

```
"%s\fd.exe"     → 執行檔路徑，同時作為 argv[0]
"%s\%s.Vid"     → argv[1]
"%s\%s.Aud"     → argv[2]
```

`%s` 前綴一律取自 `0x643e8`。呼叫前先 Stop Audio，回來後清空 `0xa0000` 起 64,000 byte 的 VGA 畫面並重設調色盤。片名由兩個呼叫端提供：`0x2a2b0` 用格式字串 `"FD%d"` 產生 `FD1`／`FD2`，`0x1ba40` 直接傳 `"End"`，對應光碟上的三組 `.VID`／`.AUD` 檔。

## 重建規模

MSCDEX 存取層在 `0x3bade`–`0x3c891` 共 3,508 byte，Ghidra 目前定位出 15 個 function、約 1,714 byte；其餘約 1,800 byte 是未被呼叫的 MSCDEX 包裝（可辨識出 IOCTL control block `0x01`、`0x04`、`0x0C` 等命令），估計再約 15 個 function。遊戲側另有 3 個音樂介面 function 與 1 個影片外呼 function。整個子系統的 emit 規模估在 30–35 個 function。
