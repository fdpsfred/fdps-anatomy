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

## 實跑撞到的兩層行尾問題

拆完之後送出 5 支的驗證批次，第一次被擋在 parse error：`Unexpected token (425:17)`，指著我新加的那條「判定檔記錄發現不立法」。RULES 是 template literal，而我在裡面寫了 `` `needs` ``——反引號在那裡是結束符號。改成 the needs field 就過了。補了一支反引號平衡檢查當代用品，因為這台沒有 node 可以在送出前 parse。

第二次被擋在更莫名其妙的地方：

```
script contains control characters that would be hidden in the approval dialog
```

掃了一遍，檔案裡連一個 `< 0x20` 的字元都沒有（除了 `\r\n\t`）。卡了一下才想到去比對行尾：工作區是 1678 個 CRLF、0 個 LF，而版本庫裡是 0 個 CRLF、1678 個 LF。`core.autocrlf` 是 true，我前面為了復原編碼事故跑了一次 `git checkout --`，checkout 就把它寫成 CRLF 了；我的 Python 腳本用 `newline=''` 忠實保留，所以一路帶著。Workflow 讀的是工作區那份，`\r` 就是它說的 control character。

換句話說：**同一份 commit 過的腳本，在磁碟上跑不動。** 而且錯誤訊息不指檔案、不提行尾，跟原因之間隔了兩層。

轉成 LF 之後 `git status` 還是顯示 modified，`git diff` 卻是空的——那只是 index 的 stat 過期。真正該做的是在 `.gitattributes` 釘住 `tools/**/*.js text eol=lf`，比照那個檔裡既有的三條（Ghidra 快照、fdps-data、攻略站鏡像）的同一個理由。不釘的話，任何人在 Windows 上 clone 這個 repo，拿到的 workflow 腳本都是跑不動的。

## code review 抓到我兩則寫錯的判讀

跑完 review，六項發現，其中兩項就在標題寫著「更正錯誤判讀」的那個 commit 裡：

**因果寫反。** `src/saf.c` 我寫成「宣告 `_inline` 來追原始碼字面會有產出 CALL 的風險」。相反：宣告 `_inline` 正是讓它不產生 CALL 的東西，呼叫而**不**宣告才會多一條 CALL。同一批的 `movegrid.c` 反而寫對了，兩個檔互相矛盾。

**測試過度一般化。** 我把「寬度不符」寫成三處展開共通的決定性證據。reviewer 去查了 `000144e0`，說它的參數是指標。自己驗一次，確實：

```
000144ec: MOV EAX,dword ptr [EBP + 0x14]    <- 完整 dword
000144ef: MOV AL,byte ptr [EAX]             <- 讀的是指標指向的內容
```

那個 byte 讀根本不是讀參數槽。寬度不符只在形參型別比 dword 窄的時候才看得到，也就是只有 `fdps_pack_rgb` 那一列。我拿一個特例當通則寫進了「後面 471 支要照著讀」的那一節——照它去測一支形參是指標或 `int` 的 function，會把真的展開判成不是。改成以「完整框架重放」為主要判別，寬度不符降級成 `pack_rgb` 的附帶觀察。

值得記的是這兩則的性質：不是分析不夠深，是**寫下結論時把手上那個案例的細節當成了普遍規則**。跟這一整輪在追的那個洞（emitter 在 `needs` 裡發明準則、下游忠實執行）是同一個形狀，只是這次發明者是我。

reviewer 另外四項也都成立：`issues_logged` 沒進 `required`（bookkeeper 漏填就靜默記成 0，而現在沒有任何下游會重讀 verdict 檔）、ENV 的 ToolSearch 清單沒有我新規則叫 agent 去用的 `search_instructions`、WDISASM 給了相對路徑沒給根、`emit_pipeline.md` 標題還寫「五個角色」但只剩四個、以及總掃只篩 `status: open` 的話永遠看不到 `0002af60` 那筆待撤銷的 BLOCKING（它是 `resolved`）。都修了。

## 驗證批次的結果不能算數

`t22-02` 跑完：5 支全數落地、gate 全綠、**rescan agent 0 個**（23 個 agent 恰好是 recover 1 ＋ worklist 1 ＋ 5×4 ＋ report 1，沒有多餘階段）。拆除本身確認有效。

但收斂門檻**沒驗到**：那 5 支一則疑慮都沒記，`emit_issues.json` 停在 30 個位址／97 筆，跟 `t22-01` 結束時一模一樣。

這個 0 有兩種相反的讀法，n=5 分不出來——門檻生效了，或者門檻寫太緊、emitter 為了不違規而不記該記的。`t22-01` 的比率是每支約 2.4 筆，連續 5 支掛零不是可以直接當好消息收下的數字。後一種可能比拆除前的狀況嚴重，所以那一項的驗收留著沒勾，判準改成「明顯低於 2.4／支但不是 0」。

順帶：因為完全沒有寫入發生，「bookkeeper 累積那一段沒被一起拆掉」這件事其實也只有 bookkeeper 的自述佐證（5 支都回報讀寫該檔且逐 byte 相同），真正的寫入路徑一次都沒走到。這是補上 `issues_logged` 交叉檢查的直接動機。

## 沒做的事

`same_as` 鏡像 21／51 偏高——emitter 跟 reviewer 大量記到同一件事，本身就是重複工。票裡列了「決定 reviewer 是否改為只註記而不重寫」，但這一輪沒動：改 reviewer 的產出格式會影響 bookkeeper 併檔的邏輯，而 bookkeeper 那段正是總掃唯一的輸入來源，不想在同一批改動裡動兩個相依的東西。留在票裡。

`0002af60` 那筆 BLOCKING 標籤的撤銷也沒做，那是個別裁決，照新的分工留給總掃。
