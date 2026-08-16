# 用前作的 AIL 靜態庫對 FDPS.LE 做 FID 比對（票 14.1）

票 14 判出 423 個 `ail` function，證據全部是間接的：連結方向、debug 字串、共用執行期狀態。CRT 那邊有 196 筆 Function ID 命中撐著，AIL 沒有對應的東西——Miles 的 `.LIB` 不在手上。票 14.1 要做的是把前作從 `FD2.LE` 合成出來的 `ailv3.lib` 餵進同一條 FID 管線，看能不能從另一個方向得到同樣的答案。

## 先查前作，然後發現前作的坑不是這次的坑

CLAUDE.md 要求開工前先查前作。查到的第一件事是 `ailv3.lib` 只有兩個模組：前作是用 consolidated mode 打包的，`ail_code`（55,864 byte，全部 428 個 function 擠在一個 segment，一個 function 一個 PUBDEF）與 `ail_data`。這跟票 14 那 821 個 Watcom 模組完全不同的形狀，但對 FID 沒有壞處——FID 是逐 function 雜湊的，一個模組裝幾個 function 不影響。

票裡預期「Easy OMF-386 的 quirky record 一樣要先修」。實測是零改動：前作的 OMF writer 直接寫 32-bit record 型別。這條先驗猜錯了，但驗證只花了一次 `omf_patch_segdef.py`，所以留著跑而不是省掉——沉默的 no-op 是證據，跳過就只是假設。

`wlib` 拆 `ailv3.lib` 撞到 spec 早就記過的坑：路徑含連字號會被當成「刪除模組」命令，`wlib -q -l=... C:\...\fd2-anatomy\...` 直接去建了一個叫 `fd2.lib` 的新檔。`extract_ail_lib.py` 因此先把 lib 複製到工作目錄再動。

## 第一次跑：`createNewLibraryFromPrograms` 回 null，沒有任何錯誤訊息

匯入 `ail_code.obj`、跑分析、建 `.fidb`——`FidPopulate` 的改寫版噴 NullPointerException，因為 `createNewLibraryFromPrograms` 回傳 null。

翻 `FunctionID-src.zip` 才看懂：`FidServiceLibraryIngest.create()` 對每個程式做一次 `checkLanguageCompilerSpec`，`languageId.equals(program.getLanguageID())` 不成立就 `continue`。全部被跳過，`result` 就一直是 null，然後回傳 null。**沒有任何一行訊息說發生了什麼。**

原因是票 14 的 `FidImportBatch` 在匯入之後會呼叫 `setLanguage` 把每個模組拉成 `x86:LE:32:watcom`（那是本專案自己加的 language，`x86watcom.ldefs`），而我這次是用 MCP 的 `import_file` 匯的，停在 `x86:LE:32:default`，卻照票 14 的跑法傳了 `x86:LE:32:watcom`。

本來想把已匯入的程式 `setLanguage` 修回去，但那時它已經跑完自動分析，語言轉換對已經反組譯的程式風險比較高；想改成刪掉重匯，`delete_file` 一直回 `ail_code.obj is in use`——MCP 的 `ProgramScriptService` 把它掛著當 consumer，`close_program` 說 `released_cache: false`，清不掉。

最後的解法是改 `FidPopulateAil`：語言不由參數指定，從模組自己讀，並且在模組之間語言不一致時直接停下來報錯。查詢端不受影響，因為 `FidFile.canProcessLanguage` 用的是 `ProcessorSizeComparator`，只比處理器與位元數。這比「傳對參數」穩：傳錯參數的症狀是靜默失敗，讀出來就不可能傳錯。

## 第二次跑：386 個命中，但有 10 個函式的失敗是我自己造成的

386 個命中，363 個落在 `pool_ail`，全部 full-hash 相同。看起來很好，直到去看那 60 個沒命中的。

排前面的是 `AIL_shutdown`、`AIL_set_preference`、`AIL_get_real_vect`、`AIL_set_real_vect`、`AIL_restore_USE16_ISR`、`AIL_register_timer`、`AIL_install_DIG_INI`、`AIL_allocate_file_sample`、`AIL_set_sample_file`、`AIL_load_sample_buffer`——十個在 lib 裡有同名 function 的公開 API，而且 code unit 數兩邊差一點點（34 vs 37、61 vs 66、52 vs 63）。同名、同角色、大小差一點，這不像「不同 codegen」，像邊界對不齊。

是邊界對不齊。前作的 OMF emitter 替每個被引用的函式中途位址發一個 `L_<函式>_alt_<位移>` 符號（244 筆 mid-fn alt-entry 的產物），Ghidra 的自動分析把這 46 個符號全部當成 function 起點，含有它的那個 function 的 body 就被切短了。切短的 body 雜湊值當然跟 `FDPS.LE` 裡完整的同一段不同，於是**安靜地比不上**——這正是最難發現的失敗形式，因為「沒命中」看起來就像「沒有對應的東西」。

`FidDemoteAltEntries.java` 把它們降回 label 再重建 function。重建走位址遞減順序（後一個 function 先存在，前一個才不會把它吃掉）。

第一版寫壞了兩處，兩處都是「以為刪掉再建就等於原樣」：

- 我把 thunk 排除在重建清單外，結果 `AIL_internal_get_isr_lock_count` 那三個 5-byte JMP thunk 隨著它們指向的 function 被刪一起消失了。Ghidra 刪 function 會把指向它的 thunk 一併帶走。改成從**符號**而不是從現有 function 收集進入點就對了，順便讓腳本可以重跑。
- 全部降級之後有 732 個 byte 落在任何 function 之外：有 18 個 alt entry 只被別的 function 跳進來，含有它的 function 自己的流程根本走不到那段程式碼。這些要還原成 function。判準寫成涵蓋率——降級、重建、看還有沒有沒被涵蓋的 byte，有就把該 alt entry 還原——而不是事先猜哪些該留。

修完 55,864 byte 全部被涵蓋，整份 binary 的命中位址從 386 上到 397。代價是 `AIL_sample_playback_rate` 反過來從命中變成沒命中（lib 這邊合併之後 224→241，`FDPS.LE` 那邊 Ghidra 切在 224），這是同一枚硬幣的另一面，沒有一個「正確」的邊界可言。

## 命中落在別的 pool：24 個，17 個是票 14 判錯

用 threshold 0 查詢（要看到全部候選，不只是預設的 14.6 以上），有 24 個命中落在 `pool_ail` 以外。這是逐 function 的判斷，所以照 ADR-0002 與 ADR-0007 寫了一支 workflow，一個 function 一個 agent，證據包事先攤好。

結果乾淨地分成兩堆：

- **7 個維持 `crt`**。`crt_malloc`、`crt_free`、`crt_close`、`crt_remove`、`crt__strupr`、`crt__toupper`、`crt__tolower` 全部命中同一個 `AIL_internal_isr_eflags_restore_thunk_37f8b`，分數 5.34。這七個的 body 都是同樣的 14 byte「取一個堆疊參數、call、清堆疊、return」轉接，FID 把重定位過的運算元遮掉之後雜湊完全一樣。證據包裡放了 `module_matched_n_functions`（同一個 lib 模組對上幾個不同的 FDPS function）就是為了讓 agent 一眼看出這是形狀撞號而不是識別。
- **17 個改判 `ail`**，分數 28–354、單一候選、full hash 相同。其中 16 個原本是 `fdps`，1 個原本是 `binary_artifact`。

最後那一個值得記：`0003dc2f` 被票 14 判成「共用 epilogue 入口」，理由是它 pop 了三個自己沒 push 的暫存器、拆了自己沒建的 frame，所以不可能是獨立可呼叫的常式。那個觀察是對的，錯的是從那個形狀推出的 pool。它唯一的資料參照 `0x00069e6c` 是 AIL debug logger 的計數器區塊第一格，而票 14 的判斷用的是「不在 0x70000 附近就不是 AIL」這條——當時還不知道 `0x69e6c`–`0x69e84` 這個範圍。九個跳進它的來源全部是已定案的 AIL 公開 API。前作把同樣的 13 個 byte 叫 `AIL_internal_log_decrement_nesting`。

所以 `binary_artifact` 的判準要收緊一句：它指的是**這次連結**產生、沒有原始碼對應的東西。廠商 object 內部就已經合併好的尾端片段隨著 lib 一起進來，屬於那個 lib。

票裡只要求處理「命中與判定不一致」的個案，但比對做完之後發現票 14 留下的 8 個中信心判定裡有兩個（`00044e0f`、`00052d2b`）現在有位元組相同的單一候選命中——pool 沒有爭議，但知識庫寫著「中信心」而證據已經不是這樣了。與其在頁面上寫「這兩個其實有佐證但判定檔沒改」，直接用同一支 workflow 再跑兩個 agent；兩個都升到高信心，pool 不變。`build_contradiction_worklist.py` 因此多了 `--extra`。

## 兩處未驗證的風險

**混音分派表**。`0x52c5c`–`0x54823` 的 136 個 function 全部命中，全部單一候選、full hash 相同，其中 132 個正好是兩張分派表 slot 指到的那 132 個。這一項可以直接關掉。

**純重定位 thunk**。`pool_ail` 裡 body 完全由重定位欄位構成的只有兩個：`0003dcb0` 與 `0003de38`，各是一條 `JMP rel32`——1 個固定 byte 加 4 個重定位 byte，FID 連雜湊都算不出來（門檻是 4 個 code unit）。但「不能雜湊」不等於「不能判定」：它們的目標可以。`0003de38` 指向 `00044f42`，而 `00044f42` 命中 `AIL_internal_get_isr_lock_count_3eeda`，正好對上前作記載的 thunk + body 配對。`0003dcb0` 指向 `00044dc0`（`PUSHFD/POP EAX/CLI/RET`），lib 裡沒有這一段。

## FDPS 有而前作沒有的那一批

對帳做完之後剩下的差額不是雜訊，是一整塊模組。`0003c984`–`0003d176` 那 11 個 function 在 `ailv3.lib` 裡完全沒有對應的東西，而其中 `0003ccf8` 是一支把 4-byte 簽章拿去跟 `"LX"` 比對、然後照 0x3c／0xac／0x18／0x8 的欄位走 header 與 chunk 的載入器——LX 是 Linear eXecutable，也就是 32-bit 保護模式的驅動程式映像格式。`0003ccf8` 與 `0003c9db`、`0003c9eb` 沒有任何呼叫者，是連結器整包抽進來的死碼；但同一塊裡另外八個是活的——`0003cb93` 有 37 個呼叫端、`0003cbaa` 29 個、`0003c984` 17 個——而且呼叫端全部在 AIL 範圍內。

一開始以為整塊都是死碼，因為第一個看的 `0003ccf8` 沒有任何 xref。逐一查完 11 個之後才發現只有三個是——「這一塊都是死碼」是從一個樣本外推出來的，而且錯了。

這對 ADR-0004 是實質的補充：沿用前作的 lib 不等於 FDPS 的 AIL 就齊了。

## 兩個工具面的教訓

第一個是 `run_ghidra_script` 吃的是 MCP 當下的 current program。這一票為了比對把 `ail_code.obj` / `ail_data.obj` 匯進同一個 Ghidra 專案，於是每一輪落地 agent 的第一次 `ApplyPoolVerdicts` 都撞上腳本自己的守衛（`expected FDPS.LE, got ail_data.obj`），三輪都重跑一次。守衛在任何寫入之前就擋下來，所以沒有損害，但 workflow 的提示現在明講「每一次呼叫都帶 `program="FDPS.LE"`」。

第二個是統計的形狀會騙人。第一版的對帳把「命中的程式庫 function」只算最高分那一個，得到「390 個 FDPS function 只對上 354 個程式庫 function」，看起來像前作少了 74 個。實際上 AIL 的公開層是一整族同形狀的 debug log wrapper，FID 遮掉字串指標與呼叫目標之後十幾個 wrapper 雜湊相同，`max()` 每次都挑到同一個名字。改成「候選清單裡的每一個都算數」之後是 390 對 390，程式庫這邊真正沒有對應的是 38 個。差 36 個的來源是選最大值這個動作本身，不是資料。
