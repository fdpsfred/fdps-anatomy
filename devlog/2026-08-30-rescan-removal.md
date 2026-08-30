# 拆掉每批回掃，順便發現一條沒人審過的自訂準則

t22-01 那批（40 支，196 個 agent，17.4M token，13.5 小時）跑完之後，使用者看著 workflow 的即時進度說：已經冒出 25 個 rescan agent 了，這代表前面的 emit/review 根本無法達到肯定的結論，或者 rescan 在掃的都是無關緊要的東西。前作 FD2 只靠 emit/review 就產出幾乎完全正確的 code，高度懷疑現在的 rescan 是不必要的浪費支出。

## 第一個判斷錯在哪

我先去數 `emit_issues.json`，得到 30 個位址、97 筆條目、已解 51／未解 46。第一次量的時候回掃還在跑，數字是 39／58，比例倒是一樣。

拆解那 51 筆已解：其中 21 筆是 `same_as` 鏡像——emitter 跟 reviewer 各記了同一件事，settle 的時候一筆帶答案、另一筆指過去。所以真正的獨立結論是 30 筆。

然後用 regex 掃 `settled_by` 有沒有引用 `verdicts/<addr>.(emit|review).json`，分成兩類：

- 9 筆真的引用了鄰居的判定檔
- 21 筆用的是 Ghidra 唯讀查詢、出貨資料檔、知識庫，或者 emit 那一輪自己 build 出來的 `.OBJ`

第一版的分類我用的是「`settled_by` 裡有沒有出現 verdicts 這個字」，那個 regex 太寬——`workspace/code_emit/out/objs/PALETTE.OBJ` 這種路徑不含 verdicts，但同一段文字後面可能提到別的 verdict，結果把靠 WDISASM 反組譯 OBJ 解掉的算成鄰居相依。改成比對完整檔名 pattern 才對得起來。

所以使用者的懷疑對了七成，但診斷要修正：rescan 沒有在掃無關緊要的東西，30 個 agent 裡 22 個確實解掉了東西，而且證據都很硬（掃 12 個 caller 的實際 immediate、拿 WDISASM 逐 byte 比對、從出貨的 M320 地圖量出 26×56）。問題是**收斂點放錯位置**——七成的結論，emit 當下手上就有答案所需的全部材料。

代價的形狀也不是「多做了一次掃描」。掃描在哪做都一樣貴。省得掉的是 rescan agent 得從零重讀這支 function、它的兩份 verdict、它的 assembly，只為了做一件 emitter 當時滿手材料的事——一整份重新建立的 context，每支一份。

## 岔出去的那條線才是真正的收穫

使用者接著問：那 22 筆有解掉東西的 case，是有回頭去改 emit 出來的 code 嗎？

去數 rescan 的 commit：25 個，全部只碰 `emit_issues.json`，沒有一個動到 `src/` 或 `tests/`。設計上就是這樣，rescan 讀鄰居已經做好的判定，發現 code 有問題只准記成 blocking 退回 emit 輪次。

然後掃「blocking」這個字，30 筆條目命中。第一版的分類腳本想用 `(no|not a|non-)blocking` 這種 negative lookahead 去分辨真假，結果 REAL/NEGATED 分得亂七八糟——`Non-blocking for the emitted C, which is unchanged` 這種寫法前面沒有我列的否定詞。改成把每個 blocking 前後 130／90 字元印出來人工看，才找到唯一一筆真的：`0002af60`。

那筆說 `src/palette.c` 的 open-coded RGB 打包其實是 `fdps_pack_rgb` 的編譯器 inline 展開，所以應該改成呼叫它，標 BLOCKING。

我把這個結論當真了，寫進票 22.1 當驗收條件（「確認 `src/palette.c` 改成呼叫 `fdps_pack_rgb`」），還額外開了一條「blocking finding 沒有退回工作清單的路徑」。後面那條是對的，前面那條是我照單全收了 agent 的判斷。

使用者直接問：為什麼會有「改呼叫」這種事情？emit 出來的 code 是完全依照 assembly 寫成的，除非 emitter 和 reviewer 都做錯，否則為什麼還會有呼叫到不同 function 的分歧？

去看 `src/palette.c:223`，emitter 自己寫得清清楚楚：

> THE PACKING IS OPEN-CODED, and that is not an oversight. ... Writing the call instead would be functionally identical; writing it out is what the assembly does.

emitter 沒錯。reviewer 也沒錯。錯的是那個 BLOCKING 標籤。ADR-0001 訂的是功能等價，明講暫存器配置這類 binary 層級差異不要求對齊；open-coded 與 inline 展開在該標準下完全等價，而 rescan 自己在同一段文字裡寫了「functionally neutral／no test expectation moves／nothing about the game's behaviour is at risk」——它的證詞否定了它的標籤。

而且照它說的改會更遠離原版：不跟著宣告 `_inline` 就會產出一條原版沒有的 `CALL`。現況反而精確對上原版——image 裡沒有東西呼叫 `0002af20`，重建裡也沒有東西呼叫 `fdps_pack_rgb`。

根因去 emit verdict 的 `needs` 欄位找到了：emitter 自己訂了一條規則，「另一處 inline 展開的證據會 settle 它——如果同樣的 temp-copy-into-parameter-slot 形狀出現在第二個呼叫點，`_inline` 的讀法就成立，**而這個呼叫點就該改成呼叫 `fdps_pack_rgb`**」。rescan 達成了那個條件，就照著執行。

一條在判定檔裡臨時發明的準則，被下游當成義務忠實履行，中間沒有任何一關拿它對過 ADR-0001。pipeline 的每一關都在檢查「有沒有照規則做」，沒有一關在檢查「這條規則本身對不對」。這比我原本以為的「blocking 沒有退回路徑」根本得多——退回路徑該不該有是後話，先要有人擋住錯誤的準則被發明出來。

## 那一輪真正值錢的是證據

`0002af60` 那筆的推導本身是好東西，被錯誤的標籤蓋掉了。它證明的是：**`-oe` 不在旗標組裡，但遊戲段確實有 inline 展開，因為原始碼用 `_inline` 要求了，而 Watcom 10.0a 在 `-od` 之下照樣履行。**

指紋是呼叫端框架裡多出一份 callee 的完整框架。決定性的細節是寬度不符：存進參數槽的是 dword，讀出來的是 byte——「提升過的引數落進形參宣告為 `unsigned char` 的 4-byte 參數槽」。巨集是文字展開，根本不會配置參數槽與結果槽，所以三者分辨得開。

三個實測展開處：`0002af60` 展開 `fdps_pack_rgb`，`00014550` 展開 `fdps_saf_frame_count` 與 `fdps_saf_get_frame`。

這件事推翻了一個已經落地的判讀。去 grep `src/`，兩處帶著相反的結論：

- `src/saf.c:87` —— 「the build is -od, which does not inline: what the original compiled from held this code textually」
- `src/movegrid.c:72` —— 「the build used -od, so the compiler did no inlining and the expansion is in the original source」

兩處的 C 都不用動（行為等價），但註解裡的推論是錯的，而 `src/` 是之後解析遊戲行為的主要依據，錯的推論留在那裡會被下一個讀者當成已知事實。都改掉了。

## 拆除本身

使用者決定整段拿掉，改成 514 支全部落地後一次總掃。

查 ADR-0007 第四條的原文才發現這不牴觸：條文寫的是「**工作結束前**要把所有低信心或留有未決問題的判定挑出來重讀一次」。一批不是一項工作。每批跑一次回掃是 `emit_ticket22.js` 自己採的嚴格讀法，總掃反而是條文的字面讀法。我原本在票裡把這個當成需要處理的衝突，白擔心了。

拆的時候踩到 memory 裡那條坑的加大版。要刪兩支 prompt builder，我用 PowerShell 的 `Get-Content` 讀進來、切掉行範圍、`Set-Content -Encoding utf8` 寫回去。跑完檢查 `git diff`，整份檔案的中文字串全毀：

```
-    '     emit: ' + fn.name + ' @ ' + fn.addr + '，reviewer 通過、build gate 通過',
+    '     emit: ' + fn.name + ' @ ' + fn.addr + '嚗eviewer ???uild gate ??',
```

而且順便加了一個 BOM。原因是 PS 5.1 的 `Get-Content` 在沒有 BOM 時用系統 ANSI codepage 讀，這台是繁中 Windows 所以是 CP950，UTF-8 的中文被當 Big5 解，寫回去就固化了。memory 裡原本那條寫的是「改 Markdown 不要用 PowerShell 字串」，範圍記窄了——不是只有寫入字串會出事，**讀**任何 UTF-8 檔也會。

`git checkout` 復原重來，改用 Python 明確指定 `encoding='utf-8', newline=''` 做行範圍刪除，中文與 CRLF 都活著。

沒有 node 可以做語法檢查（PATH 上沒有，常見安裝位置也翻過了），只好做結構檢查代替：`phase()` 呼叫與 meta 宣告的 phase 集合完全一致、六個已刪識別字沒有殘留參照、括號平衡與 HEAD 版本一模一樣（braces -1 是既有的，字串裡有個 `}`）。這不等於 parse，真正的驗證要靠實跑一批。

## 沒做的事

`same_as` 鏡像 21／51 偏高——emitter 跟 reviewer 大量記到同一件事，本身就是重複工。票裡列了「決定 reviewer 是否改為只註記而不重寫」，但這一輪沒動：改 reviewer 的產出格式會影響 bookkeeper 併檔的邏輯，而 bookkeeper 那段正是總掃唯一的輸入來源，不想在同一批改動裡動兩個相依的東西。留在票裡。

`0002af60` 那筆 BLOCKING 標籤的撤銷也沒做，那是個別裁決，照新的分工留給總掃。
