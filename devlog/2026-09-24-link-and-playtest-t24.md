# 票 24 第一段：遊戲本體連結、並排遊玩的啟動器、當機定位

## 開工前查前作

FD2 有 `tools/fd2_build/build_fd2.py`（src-only 正式建置）與 `rebuild_info/build_test/playtest_bugs.md`（實機解過的 8 類 bug）。後者的 A–H 類本專案的 `emit_pipeline.md` 八類隱性契約已經收了，這次挑三類在 `src/` 抽查：AIL 的每條宣告都掛了 `modify [eax ebx ecx edx]`（`libs/ailv3/ailv3.h`）；`cd.c` 的 INT 暫存區是完整的 `union REGS` 全域，沒有拆成純量；`blit.c`／`menu.c` 的 sin／cos 沒有定義 `__NO_MATH_OPS`，一度以為漏了，但兩處註解交代了 10.0a 的 `#pragma intrinsic` 只在最佳化開著時生效，而遊戲段是 `-od`——掃遊戲建置的 `BLIT.OBJ`／`MENU.OBJ` 也確實沒有任何 `IF@` 參照，走的是具名函式。FD2 的 `fd2_play/` 是靠 `-DFD2_REPLAY` 編進 replay hook 的決定論對拍，本專案 ADR-0003 禁止生產碼的測試 hook，所以不沿用。建置腳本的形狀照 `build_fd2.py`：只編 src、main 不改名、單次連結、未解符號就是錯。

## 連結一次就過

`build_game.py` 重用 `build_emit.py` 的原始碼掃描與命名檢查、`link_ail.ALIASES`、`build_min` 的 DOSBox-X 機制。第一次建置 35 秒，89 個 C unit 加 6 個組語 unit、95 條摘要行全部 0 warnings 0 errors，`FDE.EXE` 367,139 byte（原版 373,301），零未解符號。票 23 結束時單元測試映像的第一次連結就已經零未解，所以這在意料之中。

接著把全域資料比對（`check_data.py`）與 RLE 組語比對（`asm_match.py`）對遊戲映像也跑一次。動機是測試映像多了 97 個 `tests/` 目的檔，連結器擺放全域的位置跟著變，佈局約束在一個映像上成立不代表另一個也成立。結果兩者在遊戲映像上都過（232 個全域、15 支組語）。`check_data.py` 原本寫死讀 `EMITTEST.*`，加了 `--image game`。兩項都登記進 gate 的新 `game` 目標，並以「首次連結」為理由記下基準值。

## 啟動器與截圖

`play.py` 替原版與重建版各開一個目錄，只差 `FDPS.EXE`。調色盤快取 `*.TMP` 刻意不複製——`main` 在 `FMer1.tmp` 不存在時才建表，帶著出貨目錄裡的舊快取，重建版的建表程式碼永遠不會跑到。

截圖第一版直接炸在 ctypes：`CreateCompatibleDC` 沒設 `argtypes`，64-bit handle 被當 int 轉，`OverflowError: int too long to convert`。補齊所有 GDI／user32 函式的 `argtypes`／`restype` 後可用。

原版開機 8／20／35 秒截圖正常（logo → 標題）。重建版同樣時間點進到同一個標題畫面。用 AUTOTYPE 在標題按兩次 Enter，兩版都進到第 1 章開場對話；同一時間點的截圖逐像素比對，只差右下角等待按鍵小圖示的 12 個像素（動畫相位）。

## 死路：PrintWindow 卡死

跑「無音效卡」（`sbtype=none`）那一輪時，整個指令五分鐘沒回來。查下去：DOSBox-X 程序還在、Windows 回報 `Responding=False`，而遊戲本身其實還在跑（log 裡 CD 音軌照常開始播）。`PrintWindow` 是請目標視窗自己重繪，視窗執行緒不處理訊息時它就無限等待。改成先問 `IsHungAppWindow`，再把 `PrintWindow` 放進有 5 秒期限的執行緒，截不到就記進 `screenshot_problems`，`boot` 判失敗。同一組參數重跑兩次都沒有再出現「沒有回應」，原因不明，只能當成模擬器視窗偶發的狀態。無音效那輪兩版的截圖也逐像素相同。

## 當機位址的座標系

`locate.py` 一開始的設計是拿 DOS/4GW 傾印裡的 `CS:EIP`，減掉 LE 物件表的基底（物件 1 = `0x10000`）去查 map。寫完覺得可疑：`0x10000` 在 DOS 常規記憶體裡，延伸器不可能把程式放在那裡。寫了一支故意當機的探針驗證：

- 第一版寫入 `0xfffffff0`：程式印出 `main=001a0040 boom=001a0010 data=001a40a8` 之後就卡住，沒有任何傾印。這條不通。
- 第二版用 `#pragma aux` 塞一個 `0F 0B`（非法指令）：DOS/4GW 1.97 印出完整傾印，`CS:IP 160:001A002A`，最後一行是 `Crash address (unrelocated) = 1:0000002A`。

所以 `main` 連結在 `0001:00000040`、執行時在 `0x1a0040`；資料物件執行時在 `0x1a4000`，與程式物件的相對距離也跟連結時不同，沒有單一差值能換回去。原本的設計整個錯了。`locate.py` 改成只收 `物件:偏移` 形式與符號名，看到選擇子大於 0x10 的 `CS:IP` 就拒絕並指向傾印最後一行。這條進了 `pitfalls.md`。

## 死路：用自動按鍵追劇情後段的分岔

讓兩版在第 30 秒起每 1.5 秒按一次 Enter、共 50 次，每 10 秒截圖。第 40–60 秒兩版一致（差異只在對話框的文字進度），第 70 秒起分岔：原版在火焰回憶場景，重建版在黑畫面轉場；第 110 秒兩版停在不同場景。

定時按鍵的落點取決於遊戲當下在不在等鍵，而兩版的執行速度本來就不同（遊戲段旗標不同），所以這個分岔分不出是時序還是真差異。本來打算改成每 3 秒截一張、只比場景順序，使用者擋下：這類判斷由使用者實際操作。記成分工規則寫進 `rebuild_info/playtest.md`：AI 的自動化只回答開機到進遊戲那一段。

## 審查抓到的三個工具錯誤

commit 前用兩個子代理分別審規範與需求，抓到三個我自己的測試沒碰到的錯誤：

- **快取沒清。** `stage` 只是「不從出貨目錄複製」`*.TMP`，但兩版目錄會保留，第一次跑完快取就留在裡面。之後重建版的建表程式碼永遠不再跑——這正是我在文件裡宣稱避免掉的事。改成每次 `stage` 都刪掉。
- **當機看不見。** `boot` 靠 DOSBox-X 的 log 掃故障，但 DOS/4GW 的傾印印在 guest 的文字畫面上，而 conf 裡的 `pause` 讓程序一直活著，所以遊戲當機也會判 PASS。上面那支當機探針其實已經示範了傾印只出現在畫面上，我卻沒把兩件事連起來。改成遊戲一回到 DOS 就寫 `GAMEEXIT.TXT`，`boot` 看到就判失敗。
- **原版位址反查會停在 case 標籤上。** `check_data.load_original_names` 也收了 `labels.txt`，快照裡有 255 個標籤落在 function 本體內（例如 `fdps_transition_slide` 裡的 `switchD_0002f75a::caseD_1`），找「前一個名字」會回報標籤而不是 function。改成只用 `functions.txt` 的起點與大小。

需求審查另外指出：票上原本寫「正式 gate 是 `--target game`」，但單元測試套件綁在 `emittest` 目標上，只選 `game` 會把它當範圍外跳過。改成修正後一律跑不帶 `--target` 的完整 gate。

## 完整 gate 翻出一個過期的基準值

commit 前跑不帶 `--target` 的完整 gate：`game`、`smoke` identical，`emittest` 與全部測試套件通過，唯獨 `ailsmoke` 報 `different`。這次沒動 `src/`，所以先查歷史：`ailsmoke` 的基準值是票 20 記的（`ba5129c`），它會編 `src/dpmi.c`，而那之後票 22 以逐支 emit 的六支 DPMI 常式取代了票 19 的手寫替身，共 6 個 commit。票 22、23 期間每次都只跑 `--target emittest`，這個目標從那時起就一直是紅的，只是沒人跑到。

差異的來源清楚、而且是審查過的 emit，不是回歸。推進前先 `link_ail.py all` 實跑一次音效（startup、DIG 驅動、取樣播放、計時器 ISR、shutdown 全 ok），再以這個理由推進基準值，重跑 `--target ailsmoke` 為 identical。教訓寫進 `build_gate.md`：只跑自己那個目標，別的目標的基準值會無聲過期。

## 留給下一段

- 開發者實際遊玩兩版，逐項確認票 24 上交接的 8 則實機檢查，含存檔雙向交換。
- 回報的偏差用 `locate.py` 與反組譯定位、修正，每次修正以 `gate.py update --target game --reason` 記錄。
- 劇情後段自動按鍵造成的分岔是否是真差異，由遊玩確認。
