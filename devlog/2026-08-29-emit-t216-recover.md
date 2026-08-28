# 2026-08-29 票 22 emit 批次 t216-recover：一支 function，一個潛伏在 gate 裡的 bug

這一輪只跑了一支：`fdps_collect_targets_in_area @ 000109f0`。開跑時工作清單剩 513 支，attempted 1、committed 1，沒有 skip、沒有未完成、沒有沒排到的、沒有拆檔、沒有中途停機。以「產出」來看這篇沒什麼好寫的；真正值得記的是兩件事——這一輪是從上一輪的殘骸上重開的，以及那支 function 花掉的唯一一個 fix round 完全沒有動到 `src/`，而是修了 build gate 自己的 bug。

## 一、上一輪被砍掉了，殘留先丟掉才開跑

`recovered` 不是空的：`000109f0 fdps_collect_targets_in_area`，since `2026-08-29 01:20`。意思是上一輪跑到這支的中途被中斷，這一輪開跑前先把它的殘骸丟掉，把它放回工作清單重跑。

丟掉的是什麼，commit 記錄裡看不出來，所以在這裡寫下來：**當時已經寫了一半的 `src/gamedata.h` 被整個丟棄**，狀態改回 `interrupted` 重新排隊（`553c258`）。這一輪的 `000109f0` 不是接續那份半成品，是從三份來源重新 emit 的全新一份。之所以要特別記，是因為 `b4136c4` 這個 commit 看起來就是一支 function 一次做完，完全不會告訴讀者它其實是第二次做——如果將來有人比對時間戳或 token 帳，會奇怪 01:20 到 01:55 之間怎麼會只產出這麼一支。

這也是票 21.6 那套「無人值守與中斷復原」第一次真的被用到。結論是它有效：偵測得出 `in_flight` 的殘留、丟得掉、重排得回去。代價是那三十五分鐘的工作全部重做，沒有部分回收——這是刻意的，半份 header 比沒有 header 更難查。

## 二、emit 本身很乾淨，gate 卻是紅的

`fdps_collect_targets_in_area` 是一支很好處理的 function：一個 counted loop 掃 map unit 陣列，曼哈頓距離（兩次 CRT `abs`）先算好再過濾，接著 `select_mode` 的四段陣營／行動狀態判斷，命中就數一個、`out_indices` 非 NULL 才多寫一個 byte。第一次 build 就 0 error 0 warning，290 個 check 全過，reviewer 十二項檢查全 pass、沒有 blocking issue。

問題出在 build gate 判它 `undefined` FAIL。

這裡有一個死路值得記：**交接過來的診斷是錯的**。手上拿到的說法是「那兩個全域沒有被 routing 認得，stub 那一趟產生了一個空的 module」。照這個說法去查，第一件事就撞牆——`Code size: 0` 確實出現在 `STUBS.OBJ` 的摘要裡，但那是一個只有資料、沒有程式碼的 translation unit 本來就會報的數字，不是空 module 的證據。把 transcript 從頭讀一遍，每一步都是對的：第一次連結如設計般報出 `data_fdps_map_unit_array_ptr` 與 `data_fdps_map_unit_count`、`undefined.json` 兩個都查到 `gamedata.c` 與型別、`gen_stubs.py` 產出宣告、`STUBS.OBJ` 0 error 0 warning 且進了 `EMITTES2.LNK`、第二次連結完全沒有 undefined 那一行、`EMITTEST.EXE` 57452 byte 跑完 290 個 check。一個未解析的全域不可能跑出這種結果。

所以錯的是 gate。`tools/build_gate/gate.py` 的 `_build_emittest` 只把 `build.out` 交給 gate，`build_and_compare` 再對這一份 transcript 跑自己的 `diagnostics()`——於是 `undefined` 這一項判的是**第一次**連結。而 `rebuild_info/build_gate.md` 第 15 行早就寫死了規則：「`emittest` 連結兩次，這一項判的是**第二次**——第一次刻意不帶 stub，它報出來的是還沒 emit 的資料與 function，是清單不是錯誤」。程式碼從一開始就沒有照這條寫，只是沒有機會發作：票 21 那支 `fdps_menu_find_first_enabled_entry` 一個全域都沒有借，`undefined.json` 是空的。`000109f0` 是**第一支引用票 23 全域的 function**，於是踩爆了。

值得強調的是這個 bug 的爆炸半徑：放著不修，從這支開始一直到票 23 做完為止，每一支借用全域的 function 的 gate 都會是紅的。也就是說如果這一輪照「gate 紅就把它當成 emit 的問題」去改 `src/`，會有幾百支 function 被改成迎合一個壞掉的檢查。這是這批次唯一一次 fix round，而且它一行 `src/` 都沒改：`git status` 只有 `tools/build_gate/gate.py`。

修法上有一個刻意的取捨：不是在 `gate.py` 裡重寫一次「哪一趟才算數」的規則，而是讓它回傳兩份 transcript 並直接委派給 `build_emit.diagnostics`——那個 reader 本來就擁有第一次／第二次的區分，也本來就有 selftest 釘住。新增的 `merge_diagnostics` 只負責把各趟 transcript 的 Watcom 摘要數字加總，再讓目標自己的 reader 覆寫 errors/warnings/undefined；單趟的目標不傳 reader，行為完全不變。理由跟票 22 前置作業裡「alias 清單不抄第二份」是同一條：規則抄兩份，就會有一份過期。另外補了四條 selftest（被 stub 的符號不該 fail、兩份 transcript 都要進摘要計數、stub 之後還剩的符號要 fail、單趟目標照舊），`gate.py selftest` 24 條全過。

**這也是為什麼 `b4136c4` 這個 emit commit 裡混著一個 `tools/build_gate/gate.py` 的改動。** reviewer 特別要求把理由寫進 devlog，就是這一段：不是順手夾帶，是這支 function 剛好是第一個把這個潛伏 bug 逼出來的人，修在別的地方都會讓兩件事對不起來。知識庫沒有改，因為 canon 本來就寫對了，是程式碼去對齊文件而不是反過來。

## 三、回掃：兩個 open issue 一個都關不掉

這支留下 4 筆 issue（emitter 與 reviewer 各記了同樣的兩件事）。回掃段跑完 `changed: false`、`resolved: 0`、`still_open: 2`，`emit_issues.json` 一個 byte 都沒改。

先記死路：**想從鄰居的 verdict 找答案，完全落空**。`workspace/code_emit/verdicts/` 目前只有 `000109f0` 與 `000160e0`（一支無關的 menu function）兩組檔。唯一的 caller `fdps_map_cursor_select_loop`、姊妹 function `fdps_collect_targets_in_range`、`fdps_build_map_unit_array`、`fdps_deploy_wave`、以及 `gamedata.c` 全都還沒有 verdict。兩個 issue 掛住的契約都還沒被人寫下來，這條路在票 22 走到那些位址之前不會通。批次跑到第二支就想靠鄰居互相解 issue，是行不通的——這件事下一批不用再試一次。

**Issue 1（`out_indices` 只寫一個 byte 的單位索引）有進展但沒結案。** 有意思的是，issue 條目問的那個字面問題——「`data_fdps_map_unit_array_ptr` 背後那塊記憶體的容量」——其實 Ghidra 自己在 `00069cd8` 的 global plate 裡早就答完了（`ghidra_snapshot/comments.txt:25583`）：那塊從來就不是固定 buffer。`fdps_build_map_unit_array` 依 `DAT_00064118 * 0x50` malloc 並用它種下 count，`fdps_deploy_unit` 依 `(count + 1) * 0x50` realloc，`fdps_load_field_chapter_resources` 直接接收 0xa00 byte（32 筆）的 roster 區塊，只有 `fdps_load_savegame` 配固定的 `0x1e00` = 96 筆。容量永遠跟著 count 走，所以問題整個化約成「count 的上限是多少」。

而那正是還缺的東西，`00064118` 的 plate（`comments.txt:25129`）明說目前讀到的東西都沒把它壓在 256 以下：種子是地圖檔裡的一個 byte、「實務上跑 0..255」，而且 `data_fdps_map_unit_count` 會隨 `fdps_deploy_wave` 追加腳本單位而長過它。要結案還需要：可達的 `data_fdps_map_unit_count` 最大值——各章的部署總數（`chapters/` 今天只有 `_index.md`），或是 `fdps_build_map_unit_array` / `fdps_deploy_wave` / `fdps_deploy_unit` 三支的 emit。

留一句給後面接手的人，因為這是這次差一點踩下去的陷阱：savegame 那條路配的是固定 96 筆，看起來很像「可存檔的戰鬥最多 96 個單位」的證據，但**固定配置不是上界的證明**，這一步沒有做，也不該替它編一個。另外要講清楚的是這件事今天沒有等價性風險：emit 出來的 C 截斷的位置跟原版一模一樣，而唯一的 caller 傳的是 NULL（`0002b7c4` 的 `PUSH 0x0`）。它是覆蓋率／可達性的註記，不是正確性缺陷。

**Issue 2（`gamedata.c` 該不該把 `data_fdps_map_unit_array_ptr` 定成 `struct fdps_unit_record *`）完全沒動。** 有先去翻可能已經定案的 canon：`code_layout.md:3` 只說 `routing.json` 是「哪個 `.c`/`.h` 擁有哪個符號」的名冊，沒有規定全域的 C 型別；`code_layout.md:66-70` 只定了 `gamedata.h` 是擁有者、並明文允許在 `.c` 之前先建 `.h`；`emit_pipeline.md:46` 與 `tools/code_emit/_index.md:14` 說 `gen_stubs.py` 的型別取自 `routing.json`，也就是 `src/gamedata.h` 現在配合的那個 `/byte *`。所以 header 與 stub 不可能吵架，這個選擇確實屬於票 23 emit `gamedata.c` 的那個人。缺的就是一份 `gamedata.c` 的 verdict。

回掃這一段沒有提出任何 `ghidra_fixes`，沒有對 Ghidra 寫入，Ghidra 每一次呼叫都一次就回。

## 四、順手記下的兩件小事

- reviewer 提了一個不值得開一輪的可讀性意見：`unit = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr + index;` 是對的（cast 綁得比 `+` 緊，所以是照 0x50 的 struct 縮放），但一眼看過去容易誤讀，把 cast 加括號會更清楚。留著，將來誰路過這行順手改。
- token 帳：這一支 `out_tok_k` 106，整輪 123。以一支「乾淨、reviewer 一次過」的 function 來說這是接近下限的數字，而清單上還有 513 支。把它當成規模估算的錨點——後面任何想讓 emitter 或 reviewer 多讀一份東西的提議，成本都要乘以五百。
