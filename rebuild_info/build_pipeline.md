# 自動化建置流程

怎麼把原始碼在 DOSBox-X 裡以當年的工具鏈全自動編譯、連結並實際執行。旗標組本身與每一項的判定依據由 [`build_flags.md`](build_flags.md) 擁有，本檔只引用；這裡回答的是「這些旗標要在什麼環境、以什麼機制送進工具，以及怎麼知道它跑完了」。

腳本在 [`tools/fdps_build/`](../tools/fdps_build/_index.md)，它同時是共用的機制實作；[`tools/ail_link/`](../tools/ail_link/_index.md) 匯入它，只換掉編譯清單與執行階段的內容。

## 一切都在 DOSBox-X 內以 DOS 版工具執行

Watcom 10.0a 同時附了 DOS 版與 NT 版的 `wcc386` / `wlink`，兩者在定案旗標下產出相同的機械碼。建置一律用 DOS 版，理由是與原版相同的執行環境本身就是條件的一部分——NT 版沒有 DOS 的檔名與命令列限制，用它會讓一整類問題到最後才爆出來。host 端的 Python 只負責產生輸入、輪詢結束、解析輸出。

**DOS 版工具的落點在版本之間會搬家，而且 10.0a 的 `wcc386` 與 `wlink` 不在同一個目錄。**

| 工具 | 10.0a 的位置 |
| --- | --- |
| `WCC386.EXE` | `BINB\` |
| `WASM.EXE` | `BINB\` |
| `WLINK.EXE` | `BIN\` |
| `DOS4GW.EXE` | `BIN\` |

9.5 家族的 `wcc386` 在 `BIN`、10.5 之後在 `BINW`，所以 guest 的 `PATH` 把 `BIN`、`BINB`、`BINW` 三個都放進去，工具以裸名呼叫。`BINNT` 刻意不放。

程式庫要用完整的那份 10.0a 安裝，`WATCOM_10.0a` 那份的 `clib3s.lib` 是殘缺副本，見 [`pitfalls.md`](pitfalls.md)。

## 掛載配置

| 磁碟機 | 內容 | 理由 |
| --- | --- | --- |
| `C:` | 原始碼（建置階段）／執行目錄（執行階段） | 工作目錄，讓命令列上的檔名保持短 |
| `D:` | Watcom 安裝根 | `WATCOM=D:\`、`INCLUDE=D:\H`，程式庫以 `D:\LIB386\...` 全路徑寫進 `.lnk` |
| `E:` | 光碟映像，`imgmount -t cdrom` | 安裝程式寫出的 `Disk.no` 內容是 `CDROM at e:`，遊戲的所有光碟路徑前綴取自那裡（[`program_info/cd_audio.md`](../program_info/cd_audio.md)），所以磁碟機代號不是隨便挑的 |
| `F:` | 建置輸出 | 與原始碼分開，產出不落回版控目錄 |

光碟在建置階段其實用不到，仍然一起掛，因為執行階段一定要有，而兩個階段共用同一份掛載定義比較不會漂移。

## 旗標經環境變數送進編譯器

編譯旗標寫進 guest 的 `WCC386` 環境變數，不展開在批次檔的呼叫行上。COMMAND.COM 的命令列在變數展開後超過約 176 字元會被**靜默截斷**，最先被吃掉的是排在最後的 `-fo=` 目的檔路徑；旗標留在環境變數裡，呼叫行就恆短。

`.lnk` 指令檔與批次檔裡的每個路徑都是 8.3。DOS 版 `wlink` 讀不到長檔名，而它讀不到指令檔時只在輸出留一行 `cannot open` 就結束，不會讓整批停下來。

## 結束偵測靠三個訊號

固定等待在這裡不能用：編譯一支 `.c` 是秒級、整份專案是分鐘級，而 guest 掛住時沒有任何東西會通知 host。改成三個彼此獨立的訊號，任一個先到就結束等待：

| 訊號 | 來源 | 判定 |
| --- | --- | --- |
| 完成標記 | guest 的批次檔／程式在最後一步寫出 `BUILD.DON` / `RUN.DON` | `completed`，唯一算成功的結果 |
| 程序結束 | DOSBox-X 自己退出 | `exited`，沒有標記就是中途死了 |
| 心跳停滯 | guest 每進入一個步驟就改寫 `HB.TXT`，內容是步驟名 | `stall`，並報出它停在哪一步 |

整體逾時只當最後的擋板。心跳窗口刻意設得比任何單一編譯步驟長——心跳只在步驟之間前進，窗口太短會把慢的 `wcc386` 誤判成掛住。

**不能拿 DOSBox-X 的離開碼當判準。** 不論 `IMGMOUNT`、編譯器還是連結器成功與否，它一律回 0。建置的判準是產出的 `.EXE` 存在且連結輸出沒有未解符號，執行的判準是程式自己寫出來的結果檔。

保護模式故障另外從 log 掃。log 要在 conf 的 `[log] logfile=` 指定：只捕捉 stdout 拿到的是幾行初始化訊息加上一句「No logfile was given. All further logging will be discarded」，之後什麼都沒有——掃描於是永遠掃到空的，形同沒有這道保護。DOSBox-X 對 logfile 是附加寫入，所以每次跑之前要先刪掉舊的，否則上一輪的故障會算到這一輪頭上。

## `-silent` 只用在不碰音效的階段

`-silent` 是全自動化的預設，但它關掉的不只是主控台輸出——Sound Blaster 的模擬也一起停擺，驅動程式探測不到硬體。要驗證音效的執行階段因此改成不加，代價是會開一個視窗；程序照樣自己跑完自己退出，自動化沒有中斷。症狀與辨識法見 [`pitfalls.md`](pitfalls.md)。

## 前置檢查

外部相依缺一項就明確報出是哪一項，而不是讓後面的步驟以難解的方式失敗：DOSBox-X 執行檔、Watcom 安裝根與其下的 `wcc386` / `wlink` / `DOS4GW.EXE` / 標頭檔 / 三個程式庫、`clib3s.lib` 是否含 `stk386` 模組（殘缺安裝的辨識法）、光碟 `.cue` 與它指名的 `.bin`。要組譯的階段另外檢查 `wasm`，要驗證音效的階段另外檢查 `DIG.INI` 與驅動程式映像。

## 目前的驗證程式

`tools/fdps_build/smoke/smoke.c` 是最小的驗證程式，以定案旗標編譯、連結成 DOS/4G 執行檔並在 DOSBox-X 裡實際跑起來。它逐項確認的是：

| 項目 | 觀察到的結果 |
| --- | --- |
| 容器格式 | MZ stub 10,832 byte、CPU type 2、OS type 1，與 `FDPS.EXE` 同一形狀 |
| Watcom CRT 啟動 | `printf` / `fopen` 可用 |
| 型別寬度 | `int` 4、`long` 4、指標 4、`double` 8 |
| struct 對齊 | `{char, int, char, short, double}` 的欄位偏移 0/1/5/6/8、大小 16，即編譯器預設的 `-zp1` |
| `-fpi` 的浮點路徑 | 內嵌 x87 運算連結後結果正確 |
| 光碟掛載 | 從 `E:\PACK.VFS` 位移 `0x0B` 讀出 24 byte 簽章 `Dynasty Information Co.,` |
| 保護模式下的 MSCDEX | DPMI `INT 31h` AX=0300h 攜 `INT 2Fh` AX=1500h 回報 1 台光碟機、代號 4（`E:`），與原版 `main` 走的是同一條路徑 |

最後一項是遊戲能不能啟動的前提：原版三道光碟檢查的第三道就是它，不過就 `exit(1)`。

驗證光碟掛載只能靠**真的讀出磁碟上的位元組**。`IMGMOUNT` 掛載失敗時不會有任何診斷，只看命令有沒有報錯，會得到一個掛載其實沒生效卻一路綠燈的流程。

第二支驗證程式是 `tools/ail_link/ailsmoke/ailsmoke.c`，把同一套機制接到音效庫上：多一個 `wasm` 步驟、多一個 vendor 程式庫、執行階段多掛 Sound Blaster 與驅動程式檔案。它驗證到什麼由 [`ail_link.md`](ail_link.md) 擁有。
