# 2026-08-27 票 18：最小可編譯骨架與建置腳本

目標是證明工具鏈真的能用，順便把後續每個階段都要用的自動化建置流程立起來。做完的結果是一次就通，過程裡真正花時間的是兩處與直覺不符的環境事實。

## 先去翻前作

照規矩先看 `fd2-anatomy`。`tools/fd2_build/build_fd2.py` 是成熟的 src-only 建置：自產 DOSBox conf、自產 `.bat` 與 `.lnk`、`subprocess.Popen` 後輪詢 done marker。但它的結束偵測只有 done marker 加程序結束，逾時是硬等。

要的第三個訊號在另一個地方：`tools/fd2_play/run_play.py` 有完整的三訊號等待（`DONE.TXT` / 程序結束 / `HB.TXT` 停滯），還有保護模式故障掃描與 case-insensitive 的檔案查找。等於這張票要的東西前作都寫過了，只是散在兩支腳本裡。直接照抄結構、把 9.5a 換成 10.0a 的參數。

`tools/cd_scope/inventory_discs.py` 給了第三塊：光碟以 `imgmount e -t cdrom "<cue>"` 掛載這件事在本專案已經驗過，而且它的註解就寫著「DOSBox-X 無論 `IMGMOUNT` 與 `XCOPY` 成敗都回傳 0」——那條後來直接決定了驗證方式。

## 第一個坑：`wcc386` 不在 `BIN\`

第一次跑 preflight 就掛：`missing BIN/WCC386.EXE`。前作硬寫 `D:\BIN\WCC386.EXE`（9.5a 的佈局），10.0a 不是這樣——`wcc386` 在 `BINB\`，`wlink` 卻還在 `BIN\`，兩支工具分家。

`tools/build_flags/_index.md` 早就記了「DOS 版 `wcc386` 在 9.5 系列是 `BIN`、10.0 家族是 `BINB`、10.5 之後是 `BINW`」，我沒先看。改成三個目錄都進 `PATH`、工具以裸名呼叫，preflight 只確認三處之一存在。這樣換版本也不用改。

## 第二個坑：兩份 10.0a 安裝，一份是殘的

`pitfalls.md` 記著本機 `WATCOM_10.0a` 的 `clib3s.lib` 少了 `stk386` 模組（`__CHK` / `__STK` / `__GRO` / `__STKOVERFLOW`），要用 `WATCOM_10.0a_infobase` 那份。這件事沒有任何診斷會提醒，連結時只會冒出一堆解不掉的外部符號，而且遊戲模組用 `-s` 本來就不參照它們，症狀只在連進那 17 個保留堆疊檢查的程式庫模組時才浮現——換句話說最小程式很可能根本踩不到，等到票 22 才炸。

所以 preflight 不只是接受路徑，而是打開 `clib3s.lib` 找 `stk386` 這個模組名。後來這條變成 selftest 裡最有價值的一個案例：拿真正殘缺的那份去測，不是捏造情境。

## 驗證程式該驗什麼

一開始想只寫 `printf("hello")`。想了一下不行：那證明不了掛載，也證明不了保護模式下的 DPMI。票的宗旨是排除前置風險，只驗編譯器等於把風險留到後面。

最後定了七項，其中兩項是特意加的：

- **從光碟讀真的位元組**。`E:\PACK.VFS` 位移 `0x0B` 有 24 byte 簽章 `Dynasty Information Co.,`（VFS header 格式）。只看 `IMGMOUNT` 有沒有報錯是沒用的，它不會報錯。
- **DPMI `INT 31h` AX=0300h 攜 `INT 2Fh` AX=1500h**，也就是原版 `main` 的第三道光碟檢查。這是遊戲能不能啟動的前提，過不了就 `exit(1)`。一次就通，回報 1 台光碟機、代號 4，正好是 `E:`。

順帶量到 struct 預設對齊 0/1/5/6/8、總長 16，與 `build_flags.md` 記的 `-zp1` 一致；`parse_le_header.py` 讀產出的 `SMOKE.EXE`，MZ stub 10,832 byte、CPU type 2、OS type 1，與 `FDPS.EXE` 同一形狀。

## 結束偵測要自己測

`build` 與 `run` 都綠燈之後意識到一件事：這兩個結果證明不了結束偵測有效。掛住與跑完在成功路徑上長得一模一樣，如果三訊號退化成「等到逾時」，測試照樣全過。而 `run` 只跑 1 秒，輪詢週期 1 秒根本沒抓到心跳，輸出印的是 `last step: -`——看起來就像沒有心跳機制。

所以加了 `selftest` 子命令，用假的 process 物件直接驅動 `wait()`，逐一驗四種判定：done marker、程序結束、心跳停滯（並確認它報得出停在哪一步、而且確實早於逾時收工）、逾時擋板。preflight 的三種報錯也一起測。九項全過。

這一段不是為了湊測試。負面案例是唯一能分辨「機制有效」與「機制失效但剛好沒事」的東西。

## 第三個坑：故障掃描掃的是空的

全部綠燈之後隨手打開捕捉下來的 DOSBox-X 輸出看，第九行就是：

```
LOG: Logging: No logfile was given. All further logging will be discarded.
```

也就是說我照抄前作的故障掃描，掃的是幾行初始化訊息，之後什麼都沒有。它從第一天起就永遠回報「沒有故障」，而這在成功路徑上完全看不出來——正是 selftest 那一節在講的同一種失效。

前作 `run_play.py` 的 conf 裡是有 `[log] logfile=` 的，我抄結構的時候漏了這一行。補上之後 log 有 3KB 實質內容，掃描才真的在掃東西。順帶發現它是附加寫入，所以每次跑之前得刪掉舊的，不然上一輪的故障會算到這一輪頭上。

抄前作的教訓：抄結構容易，抄「為什麼要有這一行」難。

## 覆核抓到的三件

跑完 code review，三個都成立，其中兩個是我自己看了很多遍沒看到的。

`"E:\PACK.VFS"` 這個字面值裡的 `\P` 不是合法的 C 跳脫序列。Watcom 把它折成裸的 `P`，於是字串實際上是 `"E:PACK.VFS"`——磁碟機相對路徑，不是根目錄。編譯 0 warnings、`Code size` 前後一模一樣（長度沒變），而且 `build_min.py` 一律把路徑當 `argv[1]` 傳進去，這條 fallback 在自動流程裡是死的。只有手動跑、而且 E: 的目前目錄不在根，才會冒出一個假的「光碟掛載壞了」。

第二個比較嚴重：`do_build` 算了 `wait()` 的 `mode` 卻沒有用它，成功判準只看產物存在與未解符號。`do_run` 有檢查，所以 build 是唯一一個「掛住也能過」的階段。具體情境是 `wlink` 已經寫出 `SMOKE.EXE`、guest 在寫完成標記之前才卡住：`BUILD.OUT` 被截斷所以掃不到未解符號、`.EXE` 存在，於是印著 `stall in 300s` 然後回報成功，`all` 接著拿一個可能不完整的映像去跑。剛寫完 selftest 說「掛住與跑完在成功路徑上長得一樣」，然後就在隔壁函式漏了這條。

第三個是註解與行為不符：header 寫「RUN.DON 只在成功時寫」，實際上 `verdict=partial` 也照寫。修的方向選了改註解而不是改行為——把「程式跑完了」與「檢查通過了」分成兩件事，失敗時報得出是哪一項探針不合格，比含糊的「沒跑完」有用。
