# 2026-08-15 骨幹走查第一批：進入點到 main

票 12 的第二段開工。第一批走 depth 0–3 的 9 個 function，也就是從 LE 進入點到 `main` 的整條啟動鏈。

## 設計：agent 只讀，orchestrator 寫

走查是逐一判定的工作，照 ADR-0002 不能批次。實作方式是 workflow 腳本持有工作清單、每次 `agent()` 只帶一個位址；清單從頭到尾沒有交給任何一個 agent。

多一層決定是**讓 agent 完全不寫 Ghidra**，改名與 plate comment 全部由 orchestrator 事後套用。這樣做的直接理由是避開多 agent 併發寫入的風險（那是票 13 要驗的事，不該在這裡順便冒險），但實際跑完發現更大的好處是每一筆判定在落地前都被真的讀過一遍——這批就靠這個抓到兩個問題。

提示裡刻意寫死三件事：反組譯是第一手、decompiled C 只是第二意見；不准提 PascalCase 名稱（工具的警告對本專案是錯的，怕 agent 順從工具改名）；不確定就回 low 加 `open_question`，不要編用途填欄位。

## 結果

9 個全部回傳，8 個 high、1 個 medium，沒有 low。整條鏈是：

`crt_cstart`(0x43298，2 byte 的 JMP thunk) → `crt_cstart_body`(0x43310) → `crt_cmain`(0x4df4c) → `main`(0x29220)。

`main` 的 plate comment 直接把主迴圈的形狀寫出來了：檢查 `DISK.NO`、讀 `Disk.no` 三個 token、CD-ROM 檢查、進 VGA mode 13h，然後 while 迴圈跑 `0x2bae0` 抽一次事件，依 `0x69da0` 的請求碼分派（1 走 `0x2a960`+`0x2a2b0`，2 走指標表 `0x60304` 加 `0x31210`），最後 `0x29440`/`0x305a0`/`0x3c4a7` 收尾回文字模式並印 "Thank you for playing Flame Dragon Plus!!"。這比預期好——第一批就把票 12 要的主迴圈骨架拿到了。

## 兩個需要人裁決的點

**命名撞號。** `0x43298` 與 `0x43310` 的 agent 各自獨立提了 `crt_cstart`，兩邊講的都對——它們本來就是 `cstrt386.asm` 裡同一段組語，中間只隔著連結器塞的版權橫幅資料。裁決是進入點符號保留 `crt_cstart`，橫幅後面的啟動本體叫 `crt_cstart_body`，並在後者的 plate comment 前面加一段說明為什麼不叫 `crt_cstart`。

**跨 agent 的證據補洞。** `0x54dbc` 的 agent 只能給描述性的 `crt_init_amblksiz`，信心 medium，`open_question` 明說「上游 Watcom 符號名無法從 binary 確認」。但 `0x4df4c` 的 agent 從 cmain386 模組的 extern 順序（`__ASTACKSIZ, stackavail, __ASTACKPTR, __CommonInit, ___Argv, ___Argc, main, exit`）認出 `__CMain` 的第 4 個呼叫就是 `__CommonInit`。兩份證據合起來就把名字定成 `crt_common_init`。這是「agent 只看自己那一個 function」這個限制的必然副作用——單一 agent 看不到的東西，orchestrator 手上有全部九份報告時看得到。

## 兩個工具坑

**`__watcall` 不能寫在 prototype 字串裡。** `set_function_prototype` 帶 `void __watcall crt_cmain(void)` 會回 `Can't resolve return type: void __watcall`，五個 `__watcall` 的一起失敗。要用獨立的 `calling_convention` 參數。有趣的是 `__cdecl` 寫在字串裡完全沒問題，四個 cdecl 的第一次就過了，所以症狀是「一半成功一半失敗」，很容易誤判成資料型別的問題。

**標記 no-return 會產生孤立程式碼。** 落地後跑基準稽核，gate 從「孤立程式碼 0」變成 1 段 3 byte（`0x43527`–`0x43529`）。原因是把 `crt_cmain` 標成 no-return 之後，Ghidra 砍掉了 `0x43522` 那個 `CALL` 的 fall-through，`crt_cstart_body` 的 body 從 538 縮到 535，尾巴掉了出來。

一度考慮是不是 no-return 標錯了，但查 xref 發現 `0x43527` 有一條 `UNCONDITIONAL_JUMP` 從 `0x42e3a`（`crt_exit` 內部）進來——那三個 byte 是終止路徑的共用尾巴，本來就屬於這段組語，只是 Ghidra 靠流程走不到了。所以 no-return 是對的，錯的是 body 範圍。用 `setBody` 把 `0x43527`–`0x43529` 加回去，body 回到原本的 538 byte，gate 恢復 0/0。

這件事的教訓是**標記 no-return 之後一定要重跑稽核**，已記進 `tools/backbone_walk/_index.md`。如果沒有那道 gate，這 3 個 byte 會安靜地消失在 function 覆蓋範圍之外，等到 Phase A 要求「每個 instruction byte 都歸屬某個 function」時才會爆出來，而那時已經很難回想是哪一步造成的。

## 下一批

`main` 的 47 個 callee 是 depth 4。agent 建議優先走的是 `0x304e0`、`0x29660`、`0x2a2b0`、`0x2bae0`、`0x2a960`、`0x31210`、`0x29440`、`0x305a0`、`0x3c636`、`0x3c4a7`——正好是初始化、主迴圈抽事件、分派與收尾這幾條。depth 4 裡另外那 30 個清一色 56 byte 的 function 對應指標表 `0x60304` 的 30 個 entry，那是章節／場景處理常式，形狀規律，可以晚一點整批走。
