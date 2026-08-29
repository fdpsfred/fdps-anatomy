# 2026-08-29 票 21.7：把「區域變數要有名字」變成一條擋得住的規則

票 22 早就寫著「`src/` 裡不准有 `iVar1`」，但 `emit_ticket22.js` 的 RULES、emitter prompt、reviewer 檢查表**一個字都沒提**，`naming.md` 也只管符號前綴。這條要求存在於票裡，不存在於任何會執行的東西裡。

## 為什麼不拿已落地的兩支當證據

開工前先掃了 `src/`：`000160e0` 用 `index`、`000109f0` 用 `unit`／`count`／`index`／`dist`，一個預設名都沒有，看起來這條規則根本不需要做。

**這正好是不能靠它的理由。** 那是 emitter 自己的判斷，當時沒有任何規則要求、也沒有任何檢查會擋。而且那兩支都是短小、控制流清楚的 function，取名本來就容易。真正會出問題的是 Ghidra 吐出一串 `sVar1`／`local_18`／`local_14` 的那種——沒有規則的話，那時候就只剩運氣。

所以驗證刻意挑了下一支 `00010b20 fdps_map_grid_reset`，先確認 Ghidra 的反編譯輸出裡四個區域變數**全部**是預設名。

## 分兩半擋，界線是「機器答得出的問題不要問人」

票裡把這個當成待決取捨留著，實作時決定了：

**預設名稱由建置擋。** `build_emit.py` 在啟動 DOSBox 之前掃 `src/` 與 `tests/`，命中就中止。放在這裡而不是 reviewer 的理由是成本：emitter 自己會跑 `build_emit.py all`，所以它幾秒鐘就知道，而不是燒掉一整輪 review 才被退回。而 reviewer 是整條 pipeline 最貴的一段，讓它再掃一次 regex 抓得到的東西是純浪費。

實作上有一個決定值得記：**findings 寫成 Watcom 自己的錯誤格式**（`MENU.C(18): Error! E9001: ...`），寫進 `OUT/NAMES.OUT`，然後讓 `diagnostics()` 把這個檔跟兩份連結 transcript 一起讀。這樣它就走既有的通報路徑——`diagnostics()` 收 `Error!`、gate 數它、`result.json` 帶它——完全不用新增欄位，gate.py 一行都不用改。另一條路是開一個新的 `naming` 欄位再讓 gate 學會看它，那就是第二套要跟著維護的機制，而票 21.6 剛剛才因為 gate 判錯了「哪一趟連結才算數」踩過「規則抄兩份就會有一份過期」的坑。

**名不副實由 reviewer 擋**，檢查表第 13 項。這一項的寫法是整票最花心思的地方，因為兩個極端都不對：只掃 regex 等於沒做（建置已經做了），逐個變數寫證據會讓 reviewer 的輸出膨脹一倍而且大半是廢話。

最後的寫法是**搭在既有檢查項上**：reviewer 在做第 1–4 項（控制流、CALL 回傳值、calling convention、寬度與正負號）時本來就得弄清楚幾個值是什麼，第 13 項問的就是「那幾個值的名字有沒有說出你剛才的結論」，明文寫著 "this item is about those values and no others"。

## 兩個實作上的坑

**`in_*` 這個 regex 很危險。** 直覺會寫 `\bin_\w+`，但 `index`、`input`、`initial` 全部中彈——`index` 是這份程式碼裡最常見的合法名字之一。Ghidra 的實際形式只有 `in_EAX`、`in_stack_00000008`、`in_FS_OFFSET` 這三類，所以 pattern 收緊成 `in_(?:[A-Z]{2,3}|stack_[0-9a-f]+|FS_OFFSET|GS_OFFSET)`。`local_` 同理：`\blocal_[0-9a-f]+\b` 而不是 `\blocal_\w+`，這樣 `local_count` 過得去（`c` 是十六進位數字但 `o` 不是，`\b` 擋住了）。

**註解裡引用預設名不能被罰。** `/* Ghidra 把這個叫 iVar1，它是地圖格索引 */` 正是最該寫的那種註解，掃原始檔會讓它變成最貴的。所以掃描前先跑 `strip_c()` 把 block comment 與字串／字元字面值抹掉——**抹掉的部分用換行補回去而不是直接刪**，否則報出來的行號會偏掉，而一個指錯行的錯誤訊息比沒有訊息更浪費時間。這件事單獨釘了一條 selftest。

selftest 一共加了 14 條，雙向都釘：預設名要抓得到（含 `iVar1_index` 這種半吊子——前綴還在就還是預設名）、真名要放得過（`index`、`local_count`、`input`）、註解與字串裡的不算、行號要指對。**一個只會漏報的檢查跟一個只會誤報的檢查一樣糟**，前者等於沒做，後者會讓人開始想辦法繞過它。

## 實測

先在 `src/menu.c` 種一個 `iVar1`，建置在 DOSBox 啟動之前就中止，六個位置的行號都對，然後還原。

再跑 `00010b20`。結果：

```
sVar1    -> grid_width
sVar2    -> grid_height
local_18 -> cell_index
local_14 -> cell
```

reviewer 第 13 項 `pass`，證據寫的是「第 1–4 項我本來就得弄清楚的那四個值，就是被命名的那四個」，逐一對照 `grid_width` 對應 `+0` 的 header word（用 `fdps_field_load_chapter_resources` 的 `4 + width*height*2` 反推哪個是哪個）、`cell_index` 是 JG 比較的那個值所以是索引不是位移、`cell` 的步進是 `ADD 2` 所以型別讓 stride 成立。這正是設計時想要的形狀——它是在覆述自己讀 assembly 的結論，不是在對變數表發表意見。

而且它明確講了另一半：emitter 有兩件事沒能確定（旗標組會不會把迴圈不變式外提、`0x3f` 保留的低六位有沒有用），**寫成了 open_issues 而不是硬編一個名字**，reviewer 把這件事列為「規則正在運作」。這條出口是整條規則裡最重要的一句：一個有自信的錯名字比 `iVar1` 更糟，`iVar1` 至少誠實地告訴下一個讀的人沒有人知道這是什麼。

0 個 fix round，`out_tok_k` 45——比上一支的 106 少一半以上。不能就此推論規則讓事情變快（那支花掉的時間大半在追 gate 的 bug，而這支簡單得多），但至少可以說**加這條規則沒有讓 emit 變貴**，這是動手前最擔心的事。

## 順手記下的一件事

回掃段這次真的解掉了一個 open issue，方法值得記：它要確認「編譯器有沒有把 `width*height` 外提到迴圈外」，而做法不是重跑建置，是直接去讀**上一輪建置已經留在 `workspace/` 裡的 `MOVEGRID.OBJ`**，在 hexdump 裡認出 `8b 45 f8 / 0f af 45 f4 / 3b 45 f0` 這段 `MOV/IMUL/CMP` 就在迴圈頭裡。附帶一句它自己記下的：那個 `.OBJ` 是 Easy OMF-386，泛用的 OMF record walk 什麼都找不到（票 14 的已知坑），所以它是從 hexdump 讀 code byte 而不是寫 parser。這是知識庫的坑條目真的擋下一次重造輪子。
