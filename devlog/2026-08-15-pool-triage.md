# 2026-08-15 全程式 pool 判定（票 14 後半）

票 14 分兩段跑：先把 `.object1` 裡每一段未定義 byte 判完、該建 function 的建起來，再逐一判每個 function 屬於哪個 pool（`fdps` / `crt` / `ail` / `binary_artifact`）。第一段的敘事在 `2026-08-15-pool-triage-blocks.md`，這篇記第二段，以及最後那一輪把整票收尾的 workflow 呼叫。

跑完的狀態：1,347 個 function 全部有判定檔，沒有 `unknown`，`fdps` 544、`ail` 412、`crt` 379、`binary_artifact` 12。

## 這一輪實際做了什麼

`stats` 裡 `blocksJudged: 0`、`blockPasses: 0`，不是 Stage A 失敗，是它在前一次呼叫就跑完了——區塊判定檔 821 份（含 21 份因區塊縮小而退休的 `.superseded-*`）已經在磁碟上，腳本靠判定檔續跑，所以這一輪從 Stage B 接手。

Stage B 這一輪的兩輪落地：

- `applyPools:f1`：17 份新判定，全部 `fdps`，17/17 落地。改名 0 個，因為這 17 份的 `name` 欄位都是空的——`fdps` pool 的名字在票 12 的骨幹走查就取好了，pool 判定不重取。
- `applyPools:rescan1`：302 份重寫過的判定落地，25 個改名，pool 分布 `{ail: 211, crt: 55, fdps: 36}`。

`functionsJudged: 309` 是這支 workflow 這一輪記憶體裡追蹤的判定數，不是全票的 1,347。兩輪落地寫了 17 + 302 = 319 份，差的 10 份是同時出現在兩輪的：`crt`/`ail` 的數字在 rescan1 與總計完全一致，所以這一輪的 `crt`/`ail` 全部來自回掃，`fdps` 的 43 = 36（回掃）+ 17（新判）− 10（兩輪都經手）。也就是說那 17 份新判定裡有 10 份第一次判完就留著未決問題，當輪就被回掃收走了。

兩輪 gate 都乾淨：孤立程式碼 0、error bookmark 0、`.object1` 未定義 byte 0、function 1,347、calling convention `{__watcall: 1258, __cdecl: 89}`，兩輪都存了檔。

## 落地要修的東西只有兩類

一是符號撞名。回掃認出的 AIL 名字有三個與程式裡既有的符號撞到，落地 agent 一律加位址後綴保持兩者可區分：`AIL_release_channel` → `AIL_release_channel_00041f9a`、`AIL_install_DIG_driver_file` → `AIL_install_DIG_driver_file_00047000`、`AIL_sequence_tempo` → `AIL_sequence_tempo_0004a7a0`。這是 wrapper/impl 對子的必然結果——AIL 的 DEBUG build 每個公開 API 都是「印 trace 字串的外殼 + 真正做事的 body」，兩邊都想叫同一個名字。回掃自己已經發明了 `_impl` / `_worker` 後綴慣例來處理大部分，只有這三個漏網。

二是 Ghidra console 的雜訊：`C:\Users\fdpsf\ghidra_scripts` 底下有 7 支舊 session 留下的 `McpInline_*.java`，每次跑腳本都會噴一輪編譯失敗。兩輪落地 agent 都特地回報「這與本次無關、腳本仍跑完」。噪音沒有影響結果，但它會一直在，該清掉。

## 函式庫佐證管線

`crt` 判定要有函式庫佐證，用的是 Ghidra 的 Function ID：`wlib -q -x` 把 Watcom 的 `.LIB` 拆成 `.obj`，用 `omf_patch_segdef.py` 把 Ghidra 的 OmfLoader 讀不了的 Easy OMF-386 record 型別 byte 升成 32-bit 版本再重算 checksum，去重後改名成 `<key>.obj` 匯入（不做這步 `FidPopulate` 會找不到程式），820 個模組跑完分析，每個版本建一份 `.fidb`，再對 `FDPS.LE` 查詢。整條路是抄 FD2 的 `crt_fid_match/`，不是重寫的；自己寫 OMF parser 撞牆的過程記在前一篇。

要強調的是這條管線的覆蓋範圍：它只涵蓋 CLIB3S / MATH387S / EMU387 / GRAPH。**AIL 永遠不會有 FID 命中**，因為 Miles 的 `.LIB` 不在手上。這件事在回掃裡反覆出現——好幾份第一遍的判定把「沒有 FID 命中」當成排除 `crt` 的證據（正確）順手當成支持 `fdps` 的證據（錯誤），因為對 AIL 而言沒有命中是預設狀態。

## 死路一：把「參數走堆疊」當成不是遊戲碼

第一遍最常見的錯誤推論，而且錯得很一致：看到 function 從 `[ESP+4]` 或 `[EBP+8]` 取參數，就推「Watcom 預設是 register convention，所以這不是本專案編出來的碼，是外來函式庫」。

這個前提是假的。`rebuild_info/build_flags.md` 記著整個 binary 是 `-4s`，連結的是 `clib3s` / `math387s`——名字裡的 `s` 就是 stack calling convention 的 build。堆疊傳參是這支程式的**全域預設**，它區分不出任何東西。

被這個前提污染的判定至少有：`0003c9eb`（拿 cdecl-vs-register 當 `crt`/`ail` 的分界）、`00044dd9`、`0003d177`、`0005250f`、`00052823`（兩份 CLIB3S `timeutil.obj` 的 static helper 被判成遊戲碼）、`000476e0`。回掃一份一份把這條論證撤掉，有幾份的結論本身沒變，但支撐它的理由整段作廢。

反過來，真正有鑑別力的是 register save set 與有沒有 EBP frame：`-4s -od` 的 prologue 固定是 `53 56 57 55 89 e5`、第一個參數在 `[EBP+0x14]`、epilogue 是 `MOV ESP,EBP / POP EBP` 而遊戲段裡一個 `LEAVE` 都沒有。`00039960` 那份回掃直接引 `build_flags.md` 第 43 行把自己第一遍「這個 codegen 不像我們假設的旗標」翻掉。統計上也站得住：544 份 `fdps` 判定裡 454 份以那六個 byte 開頭，477 份有某種 callee-save push，而全程式 55 個「無 frame、第一件事就是讀 `[ESP+n]`」的 function 全部落在 `crt` / `ail` / `binary_artifact`。

## 死路二：`0x70000` 是 AIL 的狀態區

第一遍給 agent 的啟發式裡有一條「碰 `0x70000` 這一頁的是 AIL」。它是錯的，而且錯得夠久，讓 `0x56a0d`–`0x57a74` 那一整族 blit kernel 的判定全部掛上「這可能其實是 AIL」的未決問題。

回掃是用 xref 直接打掉的：`0x00070022` 的每一個 xref 都是 `fdps_blit_dispatch@000568db`（寫入者）加它自己的十二個 blit kernel，`0x0007002e` 同理；沒有任何一份 `ail` 判定引用過 `0x70000` 這一頁的任何位址。AIL 的狀態實際上聚在 `0x00069e6c`–`0x00069e84`（debug logger）、`0x00061500`–`0x00061510`（mixer 參數）、`0x00062270`–`0x000624a0`（trace 字串池）。`0x70000` 是遊戲自己的 BSS，`0x70022`/`0x70024`/`0x7002e` 是 blit 的 dest x / y / width，`0x7000f`–`0x7001d` 是鍵盤 scancode ring。

順帶把另一個誤會也解了：`0x56a8d`、`0x56b25`、`0x56dc9`、`0x57114` 這些 kernel 的「未決問題」其實根本不是 pool 問題，是 emit 問題——它們沒有 prologue，靠 dispatcher 預載 ESI/EDI/EDX 並共用 dispatcher 的 EBP frame。回掃把那段文字從 `open_question` 搬到 `evidence`，這樣後面的票不會以為 pool 還沒定。

## 死路三：pointer table 的 base 認錯，一次卡住將近一百份判定

這是這一輪最大的一個坑，而且它是**平行化本身造成的**。

`0x00052c5c`–`0x000544fa` 之間有大約一百個沒有 caller、沒有 callee、沒有 prologue 的迴圈，每個只有一個 DATA xref 指進來。第一遍的 agent 各自看到自己那個 slot 的位址——`0x52920`、`0x529a4`、`0x52a1c`、`0x52b40`、`0x52be0`……——就把它當成一張小 pointer table 的 base，然後去查那個位址的 xref，查不到任何東西，於是每一份判定都寫下同一句未決問題：「找不到誰索引這張表，所以無法決定 pool」。將近一百份判定，同一個症狀，同一個成因：他們看到的都是**同一張稀疏大表的中間**，不是表頭。

真正的 base 只有兩個：`0x0005285c`（128 entry，DIG 輸出格式轉換 bank）與 `0x00052a5c`（mixer variant bank）。全程式只有兩條指令索引它們，`CALL dword ptr [EAX*4 + 0x5285c]`（在 `FUN_00054765` 裡）與 `CALL dword ptr [EBP*4 + 0x52a5c]`（在 `FUN_00054564` 裡），另外 `FUN_00054823` 用 `PUSH 0x5285c` 把 `0x5285c`–`0x54823` 整段 DPMI page-lock 起來。

破口是有一份回掃不去查自己那個 slot，改用 `search_instructions` 找 `5285c` 這個 immediate 出現在哪裡。找到之後，剩下的判定就是把 slot 減 base 除以 4 算出 index，再拿 index 的位元去對迴圈本體：bit 0x10 = stereo、0x20 = 16-bit、0x40 = clamp、0x08 = 分離兩個目的緩衝、0x04 = MSB-first、0x02 = 左右聲道對調、0x01 = signed。這個解碼準到可以預測任兩個 slot 的程式碼差在哪幾條指令——`0x53844` 那份把 slot 0x7f 的七個位元逐一對到 body 上，沒有殘留。

教訓很直白：判定 agent 拿到的 packet 給了 xref，但沒有給「這個 xref 落在哪張表的第幾格」。沒有表的全域視野，一百個 agent 會各自獨立地得到同一個錯誤結論，而且每一個都很有信心地把它寫成未決問題。

## 沒有收掉的矛盾：那張 bank 現在有兩個主人

上一節的解法擴散得很好，但它擴散得**兩個方向都有**，而回掃結束時沒有機制去收斂。

`FUN_00054765`（dispatcher）的第一遍判定把它讀成 sprite blitter：把 DIG_DRIVER 的欄位讀成 sprite descriptor，把它的四個 caller 描述成「位在 `0x461d0`–`0x469f0` 的遊戲繪圖層」，判 `fdps`、信心 high。但那四個 caller 現在各自都有 high 信心的 `ail` 判定，其中 `00046940` 在程式裡的名字就叫 `AIL_uninstall_DIG_driver`。

於是回掃分裂了。大約四十份判定引用 caller 的 `ail` 判定，把自己定成或維持在 `ail`，其中二十幾份在 `_sibling_note` 裡明寫「`00054765` 的判定與它自己的 caller 清單矛盾，應該重跑，但我不能改別人的判定檔」。另外八份引用 `00054765` 的 `fdps` 判定，把自己**從 `ail` 翻成 `fdps`**：`00052c5c`、`00052c79`、`00052cac`、`00052da8`、`00052ddf`、`00052df5`、`00052e96`、`00052ee9`。加上本來就是 `fdps` 的 `000539bf`、`00053ac6`、`00054765`，這個 136 個 function 的 bank 現在是 125 `ail` + 11 `fdps`。

一張 dispatch table 不可能有兩個主人，所以這 11 份裡至少有一批是錯的。證據的天平明顯偏向 `ail`：dispatcher 的 index 是從 `[drvr+0x18]` 的 0..3 解出 stereo 與 16-bit 兩個位元，那是 AIL 的 `DIG_F_MONO_8/MONO_16/STEREO_8/STEREO_16` 編碼；`FUN_00054823` 把整段 page-lock 是中斷時間混音的必要條件，blitter 沒有理由這麼做；`FUN_00045e10`（AIL DIG driver 安裝模組的 LOCK，`ail`/high）直接鏈到 `0x54823`。但這不是我這篇該下的判斷，記在這裡是要說明**為什麼它沒被自動收掉**。

結構原因有兩條，都是設計時故意的：

1. 判定 agent 一律不准改鄰居的判定檔（`rescanPoolPrompt` 明寫「Never rewrite another function's verdict」）。這條是對的，沒有它就會出現兩個 agent 互相翻案的抖動。代價是矛盾只能靠「回到自己的佇列」修正。
2. 回掃的收集器故意只撈「信心不是 high，或沒有 pool」的判定，理由寫在 prompt 裡：high 信心判定上掛的未決問題大多是 calling convention 或出自哪個 library object，那是命名與 emit 票的事，為了追它們去重讀幾百份已定案的 pool 判定不划算。

兩條加起來的後果就是：`00054765` 是 `fdps`/high 且帶著未決問題，所以它**永遠不會回到回掃佇列**，而依賴它的下游也就沒有機會被修正。`stats` 的 `stillOpen: []` 是照第 2 條的定義算的，它的意思是「沒有 pool 還在疑問中的判定」，不是「沒有問題了」。

## 其他被標記但沒動的

回掃 agent 很守規矩，看到別人的判定有問題就在自己的判定裡留一句，然後放著。收集起來的清單：

- `00049790`：`fdps`/high，但鄰居指出它那個 `0x1d0` / `0x6d4` 的「遊戲維度」其實是 `AIL_register_MDI_driver` 配置的 MDI driver block 與 SEQUENCE 陣列。因為是 high 信心，不會自己回來。
- `0003d17d` / `0003d27f` / `0003d2ee`：`fdps`/high，但 `0003d177` 的回掃認定整個寫 `0x6037c` 的 cluster 是 `AIL_file_error` 那一族。第一遍的論證是「它包了 CRT 又有自己的 error enum，所以是遊戲碼」，那只排除得掉 `crt`，排除不掉「疊在 CRT 上的 vendor library」。
- `00054922`：`fdps`，但 `00054917`（`crt_fcloseall`）的回掃指出它是 Watcom `__full_io_exit(0)` / `fcloseall(5)` 共用的 `__closeall` worker。
- `0005506d`：`fdps`/high，唯一的 caller 是 FID 確認的 `crt__FtoS`，而且與 `00055110`（已判 `crt`）共用 epilogue。
- `0005241e`：`fdps`/high，理由是它用了 CRT 的月份表並呼叫 `crt___leapyear`——被 `0005250f` 的回掃指為誤判。
- `0004a050`：`fdps`，但它把 `0x63560` 的 `"Out of sequence handles\n"` 複製進 `0x69ef0`，那是 AIL 的 last-error buffer。
- `00047300`：這一個回掃自己修好了（`fdps` → `ail`），是同類問題唯一被收掉的，因為它第一遍信心不是 high。

共通點很清楚：**第一遍給了 high 信心的錯誤判定，這套機制修不了它**。信心是 agent 自評的，而一個只看得到自己 function 的 agent 完全可能對一個錯誤結論很有把握。

## 419 個未決問題是刻意留的

磁碟上 1,347 份判定裡有 419 份的 `open_question` 非空，全部是 high 信心。這與 `stillOpen: []` 不衝突，是同一條規則的兩面：那 419 個問題絕大多數長這樣——「上游的 Miles 符號叫什麼」「這是 CLIB3S 的哪個 object module」「packet 記 `__watcall` 但 body 走堆疊，該由哪張票修」。它們是命名票與 emit 票的輸入，不是 pool 的疑問。

另外還有 8 份信心停在 medium（`0003c984`、`0003cb6e`、`0003cbaa`、`0003dcb0`、`00044e0f`、`0004be0c`、`00052d2b`、`00055063`），這 8 份的 `open_question` 都是空的——它們的 pool 是靠模組歸屬或排除法推的，沒有 FID、沒有字串、沒有 call edge 直接指名，回掃認為在這張票的材料範圍內就到這裡為止。

## 收尾：那 11 筆真的重判了

上面那份「留給後面的」寫完之後就直接做掉了第一項。做法照建議：不重跑同樣的回掃，而是把 11 個位址當成明確的重讀清單餵給回掃段——為此在腳本加了一個 `rescanIds` 參數，它的用途就是「高信心但後續發現有理由懷疑的判定」，繞開「high 信心不回佇列」那條規則。同時在回掃的提示裡補了一段：**如果 packet 顯示零個 caller，這個 function 多半是被函式指標表指到的，決定 pool 的是誰在分派，不是函式體在算什麼**。

11 筆全部改判 `ail`，每一筆的證據鏈都收斂到同一處：table slot → 分派者 `00054765` → 分派者的四個呼叫端 `000461d0`／`000463c0`／`00046940`／`000469f0` 全是高信心 `ail` 的 DIG driver 常式，其中 `00046940` 就是 `AIL_uninstall_DIG_driver`。

落地時 `ApplyPoolVerdicts` 回報 11 筆 `REPLACED STALE TAG (pool_fdps -> pool_ail)`——這正是它為了這種情況才加的。`Function.addTag` 只加不減，改判之後若不主動移除舊 tag，一個 function 會同時掛著兩個 `pool_*`，任何以 tag 計數的清單都會超收。落地後跑 `ReconcilePoolTags` 對帳，1,347 份判定與 tag 完全一致，`functions changed = 0`。

第二項也補了：`pitfalls.md` 進了三筆——存檔 checksum 只加總 `len - 4` byte、`FDE.SAV` 的 XOR 密鑰常數、以及 Watcom 的 `%hf` 是 16.16 定點數不是 `%f` 的短版。前兩筆的細節寫進 `code_pools.md`，`pitfalls.md` 只留「會錯在哪」並連回去。

**「副產品的知識沒有出口」這個缺口本身沒有被修掉。** 這次是靠人讀 devlog 把候選撈出來再逐筆查證，不是靠 workflow 的機制。判定 agent 唯讀、且只寫自己那一份判定檔，是刻意的設計（ADR-0007 第二條），代價就是它看到的旁枝知識沒地方去。可行的做法是在判定檔加一個 `pitfall_candidate` 欄位讓收尾段統一收攏——留給下一支要寫 workflow 的票決定。

## 收尾數字

- 判定檔 1,347 份：`fdps` 533、`ail` 423、`crt` 379、`binary_artifact` 12，沒有 `unknown`
- 信心：high 1,339、medium 8、low 0
- 302 份判定帶 `_supersedes`（被回掃重寫過），419 份仍帶未決問題（皆 high 信心，屬命名／emit 票）
- 回掃提出的符號名：`crt` 203 個、`ail` 185 個；`fdps` 的名字沿用票 12 的結果
- gate 每輪都乾淨：孤立程式碼 0、error bookmark 0、`.object1` 未定義 byte 0
- `binary_artifact` 收斂到 12 個，全部是 jump island、共用 epilogue、convention adapter 或單一 `RET`；回掃把 `000448c5`、`0004473f`、`0004eeb2`、`0004ef3f`、`0003dcb0`、`00056653` 這幾個原本判 `binary_artifact` 的移進 `ail`／`crt`——判準是「有沒有原始碼可還原」，dispatch table 的一格與 vendor 手寫組語的一個 basic block 都有
- 全票四次 workflow 呼叫、2,085 個項目，agent 回報歸檔在 `devlog/runs/2026-08-15-pool-triage-blocks.json` 與 `devlog/runs/2026-08-15-pool-triage-pools.json`

## 還留著的

清掉 Ghidra script 目錄下 7 支過期的 `McpInline_*.java`。它們是更早的 session 留下的臨時腳本，檔案其實已經不存在了，但 Ghidra 的 OSGi bundle 把上一次的編譯失敗記在快取裡，導致**每一次** `run_ghidra_script` 都會噴一輪與本次工作無關的編譯錯誤。這次每個落地 agent 都得在回報裡特別聲明「這與本次無關、腳本仍跑完」，等於每一輪都付一次辨識成本。
