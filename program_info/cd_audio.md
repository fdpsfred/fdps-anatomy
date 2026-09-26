# CD 音源與光碟相依子系統

驗證對象：`FDPS.LE` 位址 `0x3bade`–`0x3c97b`（MSCDEX 存取層）、`0x30bf0`–`0x30f36`（遊戲側音樂與影片介面）、`0x29220`（`main`）。

## 結論：音樂播放屬於重建範圍

遊戲音樂是光碟紅皮書音軌，但**音軌的選曲、起播、停止、循環全部由 `FDPS.LE` 自己下命令**，不是交給外部程式。因此 CD 音源子系統必須連同重建。

MSCDEX 呼叫點用指令特徵搜不到，是因為中斷編號都是執行期資料：安裝檢查（AX=1500h）由 `fdps_cdrom_detect`（`0x3c636`）以 CRT 的 `int386(0x2f, ...)` 在保護模式直接發出，`INT 2Fh` 指令只存在於 `int386x` 依編號跳入的 256 筆「`INT n`; `RET`」stub 表（表頭 `0x4e326`，`INT 2Fh` 那一筆在 `0x4e3b3`）；device request（AX=1510h）要讓 ES:BX 指向真實模式記憶體，改由 DPMI `INT 31h` AX=0300h（Simulate Real Mode Interrupt）發出，`0x2F` 是暫存器結構裡的 BL。

不屬於重建範圍的只有影片播放：三段過場動畫由光碟上的外部程式 `FD.EXE` 播放，`FDPS.LE` 只負責以 `spawnlp` 呼叫它，以及呼叫前後關閉再重開音效與鍵盤中斷、調黑再還原調色盤（見「影片播放外呼」）。

## MSCDEX 存取層

所有 CD 命令共用同一個發送 function（`0x3bb7d`）。它填好 DPMI real-mode call structure 後發出 `INT 31h` AX=0300h、BL=2Fh，等同真實模式的 `INT 2Fh` AX=1510h（MSCDEX Send Device Driver Request），ES:BX 指向放在真實模式記憶體裡的 device driver request header。DPMI 呼叫回來若 carry flag 置位才印出 `DEVICE REQUEST FAILED!!!`；driver 寫在 request header 狀態字裡的錯誤位元這裡不看，由各命令包裝把狀態字存進 `0x69e20` 讓呼叫端自己判讀。

真實模式緩衝區由 `0x3bade` 以 DPMI `INT 31h` AX=0100h 配置兩塊各 0x20 個 paragraph（512 byte）：一塊放 request header，一塊放 IOCTL control block。request header 塊的段位址與線性位址存在 `0x69e54`／`0x69de8`；IOCTL 塊存的是段位址放在高 16 位元的真實模式 far pointer（`0x69da8`，直接填進 transfer address）與線性位址（`0x69da4`）。

遊戲實際呼叫到的命令包裝：

| 位址 | request header 長度 | 命令碼 | 作用 |
| --- | ---: | --- | --- |
| `0x3bfa5` | 26 | 3（IOCTL Input），control block `0x0A` | 取得音樂碟資訊：起訖音軌編號與 leadout 位址 |
| `0x3c0c8` | 26 | 3（IOCTL Input），control block `0x0B` | 取得指定音軌的起始位址與控制位元 |
| `0x3c34f` | 26 | 3（IOCTL Input），control block `0x06` | 取得 4 byte 的裝置狀態（Device Status） |
| `0x3c452` | 22 | `0x84` | Play Audio，帶起始磁區與長度 |
| `0x3c4a7` | 13 | `0x85` | Stop Audio |

輔助運算：`0x3bc3f` 把 24-bit 值拆成 M/S/F 三個位元組，`0x3bc78` 把 MSF 換算成 HSG 磁區位址（`(M*60+S)*75+F-150`）。

每個命令包裝都把 request header 的狀態字存進 `0x69e20`（Device Status 的 4 byte 回答另存 `0x69e1c`，沒有讀取端）。`0x3c6d0` 判讀 `0x69e20` 的 bit 9（busy），`0x3c6e8` 先查狀態再判讀，得到「音軌是否仍在播放」。

### DOSBox-X 的 MSCDEX 不讀未初始化的欄位

DOSBox-X 2026.08.02 內建的 MSCDEX（DOSBox-X 原始碼的 `src/dos/dos_mscdex.cpp`）處理 `INT 2Fh` AX=1510h 的 IOCTL Input（命令 3）與 IOCTL Output（命令 `0x0C`）時，request header 除了 `+1`（subunit，MSCDEX 先依 CX 覆寫再讀）之外只讀 `+2`（命令碼）與 `+0x0E`／`+0x10`（transfer address），宣告長度（`+0`）、`+0x12` 之後的欄位、以及超出 26 byte 的部分一概不讀；分派只看 transfer buffer 的第一個 byte（control block 代碼）。Read Audio Track Info（control block `0x0B`）只讀 block `+1` 的音軌號，無論成功與否都無條件寫回 block `+2`..`+6`（frame、second、minute、0、attr），查詢被拒時寫的是零。因此在 DOSBox-X 下，各 CD 請求在 header 與 control block 裡留下的未初始化堆疊位元組不影響結果，重建版的堆疊配置不同也不會造成行為差異。遊戲端（各請求留下哪些未初始化的位元組、`0x3bce2` 宣告的長度）已對過 `src/`；模擬器端的讀法沒有對照 DOSBox-X 原始碼或實測驗證過（[`open_issues.md`](../open_issues.md)）。

## 啟動時的光碟偵測

`main`（`0x29220`）在進入遊戲迴圈前依序做三件事：

1. `access("DISK.NO", 0)`。檔案不存在就印 `Can't found file 'Disk.No' !!!` 與 `Please use install function.` 後 `exit(1)`。
2. 以 `"rt"` 開啟 `Disk.no`，連續三次 `fscanf(fp, "%s", ...)`，第三個 token 寫進全域 `0x643e8`，成為之後所有光碟路徑的前綴。安裝程式寫出的內容是 `CDROM at e:`，因此前綴為磁碟機代號。
3. 呼叫 `0x3c636`：發 `INT 2Fh` AX=1500h BX=0（MSCDEX 安裝檢查）。回傳 BX（光碟機數量）為 0 時直接回 0；否則記下第一台光碟機代號到 `0x69dfe`，配置真實模式緩衝區並查詢音樂碟資訊，狀態字等於 `0x810C` 回 2，其餘回 1。`main` 只接受回傳值 1，否則印 `Fatal error: CDROM is not install!!!` 與 `Check your CDROM please!!!` 後 `exit(1)`。

自動化建置環境要跑起遊戲，必須同時滿足：工作目錄有 `DISK.NO` 且其第三個 token 指向已掛載的光碟機、DOSBox-X 以 `imgmount -t cdrom` 掛上光碟映像、MSCDEX 介面可用。

離開遊戲迴圈後 `main` 呼叫 Stop Audio、以 `INT 10h` AX=0003h 切回文字模式，印出 `Thank you for playing Flame Dragon Plus!!`。

## 遊戲側音樂介面

- `0x30cc0`：換片檢查與起播。先 Stop Audio，再組出 `"%s\Pack.vfs"` 並 `access` 之；檔案存在時從該容器讀出 `Pass.Dat`，取首字元減 `'0'` 得到目前光碟編號。章節索引 < 18 需要光碟 1，>= 18 需要光碟 2；不符或找不到 `Pack.vfs` 就開訊息窗，畫全域文字條目 `0x222`（換第 1 片）或 `0x223`（換第 2 片），按鍵後再畫 `0x22A` 並等 20 秒後重試；迴圈沒有逾時，直到放入正確的光碟才離開。通過後依章節查表起播。
- `0x30bf0`：以 0-based 音樂索引起播（送出的音軌編號是索引 +1），傳入 `-1` 表示停止；音樂關閉時索引一律先改寫成 `-1` 再存入 `0x69d54`。
- `0x30c50`：由我方回合、地圖游標、指令選單、商店、村莊的互動迴圈與場景調色盤循環（`fdps_cycle_scene_palette`）每輪呼叫；計時器 tick 有變動才計數，每 75 tick（25 Hz 下 3 秒）在有選曲且音樂開啟時查一次光碟狀態，音軌已播完就重新起播同一首，達成循環播放。
- `0x3c85b`：起播單一音軌。流程是 Stop Audio → 查該音軌資訊 → 由 `0x3c803` 算出播放區間（下一軌起點，最後一軌則用 leadout）→ Play Audio。

相關全域：`0x60008` 為音樂開關旗標，`0x69cf4` 為目前章節索引，`0x69d54` 為目前選定的 0-based 音樂索引（音軌編號是它 +1，`-1` 表示無）。

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

`0x30f40` 收一個片名字串，組出三條路徑後以 `spawnlp(P_WAIT, path, path, vid, aud, NULL)` 執行光碟上的 `FD.EXE`：

```
"%s\fd.exe"     → 執行檔路徑，同時作為 argv[0]
"%s\%s.Vid"     → argv[1]
"%s\%s.Aud"     → argv[2]
```

`%s` 前綴一律取自 `0x643e8`。呼叫前依序關閉 AIL（`AIL_shutdown`）、卸下鍵盤中斷並清空鍵盤緩衝、Stop Audio、把整個 DAC 調黑；回來後清空 `0xa0000` 起 64,000 byte 的 VGA 畫面、以原調色盤重設 DAC、裝回鍵盤中斷，並以 25 Hz 重新初始化音效。片名由兩個呼叫端提供：`0x2a2b0` 用格式字串 `"FD%d"` 產生 `FD1`／`FD2`，`0x1ba40` 直接傳 `"End"`，對應光碟上的三組 `.VID`／`.AUD` 檔。

## 重建規模

MSCDEX 存取層是 `0x3bade`–`0x3c97b` 的 33 個 function，上表以外多是映像內沒有呼叫端的包裝（Location of Head、Audio Channel Info／Control、UPC、Media Changed、Lock Door、Close Tray、Seek、Resume、Q-Channel 等）。重建分在 `src/cd.c`（裝置請求路徑與 IOCTL 包裝，12 個）、`src/cdtoc.c`（目錄查詢與 MSF 換算，10 個）、`src/cdaudio.c`（播放命令，11 個）；遊戲側 3 個音樂介面 function 也在 `src/cdaudio.c`，影片外呼 `fdps_play_movie` 在 `src/title.c`。
