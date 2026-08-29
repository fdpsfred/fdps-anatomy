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

## Code review 抓到一個會靜默吃掉整輪回掃的洞

命名這條規則本身 review 沒有意見，但驗證跑順帶暴露了六個 pipeline 問題，其中一個是真的會弄丟工作的。

**回掃的產出沒有人 commit，而下一輪會把它刪掉。** 落地 commit 在回掃**之前**就做完了（bookkeeper 是第四個角色，rescan 是第五個），`rescanPrompt` 從頭到尾沒有 commit 這一步，收尾的 Report 段只 `git add devlog`。所以一批跑完，`emit_issues.json` 就是髒的躺在那裡——而 `tools/code_emit/data/` 正是票 21.6 的收拾段擁有並且會 `git checkout --` 還原的六個路徑之一。

也就是說：**下一批一開跑，整輪回掃的結論就被自己的收拾機制刪掉了**，而且刪得無聲無息——Recover 會回報「沒有界線外的東西」，因為那個檔本來就在界線內。更糟的是重建不回來：支撐那個結論的 verdict 檔在 gitignore 的 `workspace/` 底下。

這個洞的形狀值得記：**票 21.6 加的收拾機制，讓「未 commit」從『下次記得 commit』變成『下次會被刪掉』。** 引進一條不變式，等於把所有既有的違反從無害變成致命——這句話我在 21.6 的 devlog 裡才寫過一次（那次是 devlog 沒 commit 會卡死下一批），現在同一個機制用第二種方式咬了一次。往後任何一段會寫檔的 agent，都要問一次「這個檔誰 commit」。

**第二個是結構性的：疑慮只會被回掃一次，而且是在最不可能答得出來的那一批。** `withIssues` 只裝本批落地的 function，所以一則疑慮的第二次機會發生在**記錄它的那一批**。但一則疑慮之所以懸著，正是因為它在問鄰居的契約——ADR-0007 第四條自己就這麼說——而那要等落地了鄰居的那一批才答得出來。結果是每則疑慮拿到一次機會、用在最差的時機，之後檔案就變成唯寫。`000109f0` 的四則從 t216 那輪起就是這樣躺著，沒有任何機制會再看它們一眼。

修法是新增一個選單階段，從 `emit_issues.json` 與 call graph 挑「仍 open 且鄰居剛落地」的位址。刻意**不是**「全部仍 open 的」：那會無界成長，每批花幾百個 agent 去重問一批資訊量完全沒變的問題，然後得到一模一樣的誠實「還是不知道」。**鄰居落地才是唯一改變了的東西，所以它就是篩選條件。**

其餘四項比較小但都是同一類——記錄的形狀不一致就會被靜默漏掉：`status` 欄位只有一個位址有（另一個位址的四則永遠不會被任何 `status == "open"` 的篩選找到）；同一個結論在 emit 與 review 兩則各存一份 1.5 KB 逐字複本、沒有 key 綁著，於是 review 抓到的那個事實錯誤（位移寫成 `+0x3f`，我自己重解 OMF 確認是 `+0x3e`；說「instruction for instruction 相同的四條」但引的 byte 是五條而且運算元用不同的 frame slot）只會被改到一份，留下另一份繼續矛盾。

還有兩則 reviewer 的觀察原本會直接消失——verdict 檔在 `workspace/` 不進版控，devlog 是敘事不是待辦，`open_issues.md` 沒有它們。其中一則是真的 bug：`tests/movegrid.c` 的 runner 跑完之後把 `data_fdps_battle_move_grid_ptr` 留在指向自己的 file-static fixture，所有 runner 共用一個 process，下一個假設 grid 未配置的測試單元會繼承一個指向別人 fixture 的活指標，然後因為錯誤的理由通過。一行還原成 NULL 就解決，但它能被發現純粹是因為這次跑了 review。

## 順手記下的一件事

回掃段這次真的解掉了一個 open issue，方法值得記：它要確認「編譯器有沒有把 `width*height` 外提到迴圈外」，而做法不是重跑建置，是直接去讀**上一輪建置已經留在 `workspace/` 裡的 `MOVEGRID.OBJ`**，在 hexdump 裡認出 `8b 45 f8 / 0f af 45 f4 / 3b 45 f0` 這段 `MOV/IMUL/CMP` 就在迴圈頭裡。附帶一句它自己記下的：那個 `.OBJ` 是 Easy OMF-386，泛用的 OMF record walk 什麼都找不到（票 14 的已知坑），所以它是從 hexdump 讀 code byte 而不是寫 parser。這是知識庫的坑條目真的擋下一次重造輪子。
