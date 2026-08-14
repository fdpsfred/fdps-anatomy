# 2026-08-14 Ghidra 狀態文字快照工具（票 02）

做 ADR-0005 的實作：把 Ghidra 對 `FDPS.LE` 的分析狀態匯出成能 diff 的純文字。

## 匯出管道：先排除 headless

第一個想到的是 `analyzeHeadless -process -readOnly`，這樣匯出可以完全獨立於 GUI。放棄了：Ghidra 專案在 GUI 裡開著，project 目錄被鎖住，headless 連唯讀開啟都會被擋。改成走 Ghidra MCP，讓匯出在那個已經開著的 instance 裡跑。

接著要決定腳本放哪。`run_script_inline` 的程式碼會被包進一個現成的 class body，所以第一次送完整的 `public class T extends GhidraScript` 直接編譯失敗（`illegal start of expression`），只能送裸的 statement——不適合放一支幾百行的工具。改試 `run_ghidra_script`，發現它接受絕對路徑，會把檔案複製到 `~/ghidra_scripts` 再跑。腳本因此可以留在 repo 裡的 `tools/ghidra_snapshot/`，符合 scripts 規範。

一個噪音來源：編譯失敗的 `.java` 會留在 `~/ghidra_scripts`，之後每次跑任何腳本，Ghidra 都會把那些舊的編譯錯誤重印一次（bundle 的 build 快取），即使檔案已經刪掉。看起來像失敗，其實不是；把試驗用的檔案刪掉之後訊息仍會殘留一陣子。

寫之前先探了兩件 API 事實：Ghidra 12.1 的 comment API 是 `CommentType` enum（`CodeUnit.PLATE_COMMENT` 那組 int 常數的年代已經過去），以及 `getScriptArgs()` 可以透過 MCP 的 `args` 傳進來，所以輸出目錄能參數化。預設值是寫死的絕對路徑——單機專案，不值得為此加設定檔。

## 真正的設計問題：排除什麼

先數了一遍，結果決定了整個檔案格式：

- 6933 個 composite 型別裡，6927 個在 `/_le/_fixup/...` 底下，一個 fixup 位址一個
- 7186 個非自動符號裡，6844 個是 `fix_off32_<位址>`
- 全部 6844 個有 comment 的位址，comment 都是 `fixup to -> <位址>`，型別全是 PRE

三族都是 LE loader 從 relocation table 產生的匯入產物，不是分析成果。曾經想過全部照收——反正排序穩定，第一次 commit 之後就不會再產生 diff——但那樣 `data_types.txt` 會是七千個各帶欄位的 struct，任何人要在裡面找一個真的 struct 都不可能，違背「diff 要看得懂」的初衷。最後用三條窄的規則排除（category 前綴 `/_le`、label 名 `fix_off32_[0-9a-f]{8}`、PRE comment 全文 `fixup to -> [0-9a-f]{8}`），並把排除的筆數寫進 `program.txt` 的 `counts.excluded.*`。這樣即使被排除的東西數量變了，仍看得見；而只要哪一筆被我們改名或搬走，它就不再符合規則，會自己浮出來。

沒有排除的是 loader 自己那幾個容器 struct（`IMAGE_LE_HEADER`、`IMG_LE_DATA` 之類）。它們在 root category，跟我們之後要建的 struct 混在一起，沒有可靠的區分依據，而且總共只有 194 行——寧可留著。

第二個判斷是區域變數。票上沒寫，但 emit 流程會大量改變數名，漏掉的話快照就無法宣稱「Ghidra 狀態同步可驗證」。折衷是只列 source 非 DEFAULT 的區域變數，基準狀態下一個都沒有，不佔篇幅。同理，`labels.txt` 只列顯式建立的符號，但 `data.txt` 逐筆列出全部 defined data，所以在某個位址套用型別或改名都會顯示出來。

順手加了 `bookmarks.txt`。全程式只有 63 個 bookmark，其中 10 個是 `Bad Instruction` error——那正是票 10 要修的反組譯損壞。放進快照，修復進度就直接是 diff。這 10 個 error bookmark 是既有狀態，本次沒有動。

## 驗證

三步：

1. 連續匯出兩次到不同目錄，`diff -r` 完全相同；檔案確認是合法 UTF-8、零個 CRLF（`data_types.txt` 帶 18 個非 ASCII byte，是 loader 型別描述裡的彎引號，正好順便驗證了編碼）。
2. 在 Ghidra 裡故意改四種狀態——function 改名、加 function tag、寫一段含中文與引號的 plate comment、建一個 label——再匯出。diff 剛好是那四行加上 `program.txt` 的兩個計數，中文與換行都原樣保留。
3. 把四項改動全部還原，再匯出一次，與步驟 1 的基準 byte 相同。

第三步不只是驗證匯出，也順便確認了還原是乾淨的——function tag 除了從 function 上移除，還要從 program 的 tag 表刪掉，否則會留下一個沒人用的 tag 定義。
