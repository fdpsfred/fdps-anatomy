# 命名慣例

**驗證對象**：Ghidra 內的 function 與 global 符號名、`src/` 的識別字與檔名、以及 function 內部的區域變數與參數。命名的結論以此檔為唯一正典。

慣例沿用前作 FD2（出處是 FD2 的 `rebuild_info/src_map.md` 與 `rebuild_info/crt/symbol_inventory.md`），專案代號換成 `fdps`。與前作不同的有兩處，都寫在〈struct 型別名稱〉：遊戲的 struct 加 `fdps_` 前綴，以及不用裸 `crt_` 前綴。

## 兩條鐵則

**Ghidra 名稱與 C 名稱逐字相同。** 同一個東西在 Ghidra 裡叫什麼，在 `src/` 裡就叫什麼，一個字元都不差。這是兩邊能互相對照的唯一基礎，也是 emit 完之後還能回頭查證的前提。

**程式庫函式就叫程式庫的名字，不加任何前綴。** 名字的用途是讓 wlink 拿去解析真正的 `.LIB`，所以拼法、底線、大小寫逐字照抄：`memcpy`、`_nmalloc`、`__CHK`、`__STKOVERFLOW`、`IF@COS`。`IF@` 這一族的 `@` 是符號的一部分，保留。加上 `crt_` 之類的前綴會讓名字解析不到，而且切斷與程式庫的對照關係——那個對照正是 pool 判定的證據本身。

## 符號前綴

| 對象 | 形式 | 例 |
| --- | --- | --- |
| 遊戲邏輯 function | `fdps_` + snake_case | `fdps_rle_blit_scaled`、`fdps_shop_draw_item_entry` |
| 遊戲全域資料 | `data_fdps_` + snake_case | `data_fdps_item_effect_table_ptr` |
| Miles AIL 全域資料 | `data_ail_` + snake_case，或上游原名 | `data_ail_mix_loop_dispatch_table` |
| Watcom CRT 真符號 | **程式庫原名，無前綴** | `memcpy`、`_nmalloc`、`__CHK`、`IF@COS` |
| 程式庫 object 內的 file-static | `L$N_<模組>_<用途>` | `L$1_rand_seed_ptr` |
| 行為等價但比對不到程式庫 object、必須手寫的 | `crt_equivalent_` + snake_case | （`FDPS.LE` 沒有這類符號） |
| Miles AIL 公開 API | `AIL_` + 上游原名，大小寫照舊 | `AIL_startup`、`AIL_set_sequence_loop_count` |
| AIL 內部 helper 與 inner worker | `AIL_internal_` + 描述，或 `AIL_internal_<公開名>_inner` | `AIL_internal_alloc_and_commit`、`AIL_internal_start_sample_inner` |
| AIL 混音分派表 callback | `AIL_internal_mix_finalize_<兩位十六進位 slot>` / `AIL_internal_mix_loop_<同>` | `AIL_internal_mix_finalize_01` |
| 連結器與編譯器產物 | `binary_artifact_` + 描述 + `_<位址>` | `binary_artifact_church_menu_switch_table_35b84` |
| struct 型別名稱 | 遊戲的用 `fdps_` + snake_case，程式庫的用原名，不加 `_t`（見〈struct 型別名稱〉） | `fdps_unit_record`、`SAMPLE` |

CRT／AIL／連結器產物三類**不套用** `fdps_` 慣例。注意這裡沒有「`crt_` 前綴」這種東西——CRT 的東西要嘛叫程式庫原名（無前綴），要嘛是 `L$N_`，要嘛是 `crt_equivalent_`，三選一。

## 資料符號的 pool 怎麼判

資料符號沒有 function 那樣的 FID 比對可用，判準是**誰取用它**：只有 `pool_ail` 的程式碼碰得到的全域就是音效庫的，只有 `pool_crt` 碰得到的就是 CRT 的。跨 pool 取用時要判斷誰是擁有者、誰只是訪客——遊戲去戳一個 CRT 變數，那個變數仍然是 CRT 的，仍然用程式庫的名字。判定不出 CRT 程式庫符號時走 `L$N_` 或 `crt_equivalent_`，不要硬湊一個像 CRT 的名字。

**字串字面值不是全域符號。** Ghidra 給的 `s_` 標籤留著就好；取了名的字面值一律用 `binary_artifact_string_literal_<描述>_<位址>`，標明它是編譯器產物。兩種都一樣：重建後它們是敘述句裡的字面值，不是具名全域，當成全域 emit 等於憑空造出原版沒有的符號。指向它們的指標表則另當別論，那是實實在在的具名資料。

## struct 型別名稱

型別名稱套用的是與符號完全相同的那兩條鐵則，不另立一套前綴。**不加 `_t` 後綴**（前作的 `runtime_char`、`item_effect` 也不加），名稱裡同樣不能有位址。

| 對象 | 形式 | 例 |
| --- | --- | --- |
| 遊戲的結構 | `fdps_` + snake_case | `fdps_unit_record` |
| 程式庫的結構（CRT 與 AIL） | **程式庫原名，無前綴** | `tm`、`FILE`、`__iobuf`、`rt_init` |
| 判定不出程式庫名稱、只存在於單一 object 內的結構 | `L$N_<模組>_<用途>` | `L$N_emu387_state` |

**`crt_` 不是前綴。** 唯一合法的形式是複合前綴 `crt_equivalent_`，意思是「行為等價但比對不到程式庫 object、必須手寫的東西」。寫 `crt_tm`、`crt_file` 這種名字是錯的，理由與程式庫函式那條鐵則同源：重建後的 `.c` 是從 `<time.h>`、`<stdio.h>` 拿到這些型別的，加了前綴就會讓 Ghidra 名稱與 C 名稱對不起來。這一點與前作不同：FD2 把 `crt_` 與 `crt_equivalent_` 並列為 CRT 層前綴，庫裡也有 `crt_emu387_int7_fptan_opcode_worker_4c630` 這類裸 `crt_` 名字，本專案不沿用。

「判定不出名稱」與「懶得查」是兩回事，這點與符號那條完全一樣：先去 Watcom 10.0a 的標頭找，找不到才落到 `L$N_`。

前作沒有替遊戲型別加專案前綴（它叫 `runtime_char`，本專案叫 `fdps_unit_record`），所以兩邊的遊戲型別名稱不是逐字對應的關係，對應寫在 [`program_info/data_structures.md`](../program_info/data_structures.md) 的「與前作 FD2 的對應」。程式庫型別兩邊都用原名，是逐字相同的。

**唯一的無前綴豁免是 C 進入點 `main`**——Watcom CRT 的 `cmain386` 契約要求這個符號就叫 `main`。

## 兩個容易搞錯的細節

**同一支 helper 被靜態連結兩次時，thunk 用原名，body 用程式庫發佈的帶位址後綴名稱。** 廠商的 `.LIB` 裡同一個 helper 可能由兩個 object 各帶一份，連結後成為一個 5-byte `JMP` thunk 加一份完整 body。前作的庫就是這樣命名的（`AIL_internal_log_lock_acquire` 是 thunk，`AIL_internal_log_lock_acquire_3e724` 是 body），照抄：後綴是前作映像裡的位址，不是本專案的位址，本專案位於 `0x4478c` 的 body 仍叫 `AIL_internal_log_lock_acquire_3e724`。

**名字要是程式庫真的有的符號。** 判定不出 PUBDEF 就不要硬湊一個像 CRT 的名字：那是 file-static（用 `L$N_`）或必須手寫的等價實作（用 `crt_equivalent_`）。可查證的公開符號清單由 [`tools/pool_triage/fid/`](../tools/pool_triage/fid/_index.md) 的 `extract_watcom_symbols.py` 從實際連結的三個 Watcom 程式庫與啟動 object `CSTRTX3S.OBJ` 產生。

## pool 分類

每個符號歸 `fdps` / `crt` / `ail` / `binary_artifact` 四個 pool 之一，function 的判定結果與依據見 [`program_info/code_pools.md`](../program_info/code_pools.md)，全域資料的見 [`program_info/data_structures.md`](../program_info/data_structures.md)。

**不能用位址範圍判定 pool。** 前作的教訓是三類 function 在同一個 object 裡互相交錯擺放，沒有乾淨的 library／遊戲分界；本專案的位址分區（見 [`program_info/memory_layout.md`](../program_info/memory_layout.md)）同樣是從 pool 判定歸納出來的結果，分區之間仍有交錯，不能反過來當判定依據。可靠的依據是函式庫比對，輔以 caller/callee 關係與共用資料。逐一判定的要求見 [ADR-0002](../docs/adr/0002-no-batch-processing-per-function.md)。

**名字不是 pool 的證據，pool 也不決定名字。** 兩者各自有判定依據；`0003dc2f` 那個共用 epilogue 帶著 AIL 的名字而形狀像連結產物，就是這條的實例。

## Function 內部：區域變數與參數

上面兩節管的是符號——外部看得見、要與 Ghidra 逐字對應的東西。這一節管 function 內部，規則不同：內部的名字**不需要**與 Ghidra 一致，而且大多數情況下不應該一致。

**每一個區域變數與每一個參數都要有說得出意思的名字。** 反編譯器的預設名稱一個都不留在 `src/` 裡：

| 形式 | 例 |
| --- | --- |
| 型別字母 + `Var` + 序號，含指標與陣列變體 | `iVar1`、`uVar3`、`puVar2`、`auVar1` |
| 堆疊槽 | `uStack_8`、`aiStack_20` |
| 參數與區域槽 | `param_1`、`local_8`、`local_1c`、`local_res8` |
| 進入時的暫存器與堆疊 | `in_EAX`、`in_stack_00000008`、`in_FS_OFFSET` |
| 未受影響／額外輸出 | `unaff_EBX`、`extraout_EDX` |
| 位址標籤 | `DAT_00069cd8`、`PTR_...`、`FUN_...`、`LAB_...` |
| 反編譯器的偽運算 | `CONCAT44`、`SUB84`、`ZEXT48`、`__return_storage_ptr__` |

`iVar1_index` 這種半吊子不算數——前綴還在，就還是預設名稱。

**名字反映的是它在這支 function 裡的角色**（`tile_index`、`remaining_mp`、`cursor_row`），不是它的型別（`int_var`）也不是它的來源暫存器（`eax_val`）。迴圈計數器叫 `i`／`j` 可以，那是慣例不是預設名。

理由與符號那兩條鐵則同源：`src/` 是專案其他部分用來查「遊戲到底怎麼運作」的主要依據（見 [`README.md`](../README.md) 的 `src/` 那一列），一支滿是 `iVar1` 的 function 讀起來與反編譯輸出沒有差別，等於 emit 這一步沒有把任何理解沉澱下來。

**判斷不出用途時記 `open_issues`，不要編一個名字。** 一個有自信的錯名字比 `iVar1` 更糟——`iVar1` 至少誠實地告訴下一個讀的人「沒有人知道這是什麼」，而一個看起來很合理的錯名字會被當成已經確認的知識，然後被引用。

### 兩半分開擋

這條規則的執行分成機械可判與不可判兩半，刻意由不同的關卡負責：

**預設名稱由建置擋。** `tools/code_emit/build_emit.py` 在啟動 DOSBox 之前先掃 `src/` 與 `tests/`，命中就以 `E9001` 的形式報錯並中止建置，訊息帶檔名與行號。掃描前會把註解與字串字面值抹掉（保留行號），所以 plate comment 裡寫「Ghidra 把這個叫 `iVar1`，它是地圖格索引」不會被罰——會罰的話，最該寫的那種註解就變成最貴的。

**名不副實由 reviewer 擋**，是檢查表的第 13 項。它只能由讀過那段 assembly 的人判斷，所以成本落在 pipeline 最貴的一段身上，寫法上因此刻意**搭在既有檢查項上**而不是獨立掃一遍：reviewer 在檢查控制流與 CALL 回傳值時本來就得弄清楚幾個值是什麼，第 13 項問的就是「那幾個值的名字有沒有說出你剛才的結論」，不是「逐個變數表示意見」。逐個寫證據會讓 reviewer 的輸出膨脹一倍而且大半是廢話。

分工的界線是：**機器答得出的問題不要問人。** 預設名稱一個 regex 就抓得到，讓 reviewer 再掃一次是純浪費；而「這個名字是不是真的」regex 永遠答不出來，硬要它答只會得到有自信的胡說。

## 檔名

`.c` 與 `.h` 的 basename ≤ 8 字元。Watcom 的 DOS 版工具鏈沒有長檔名支援，超過就會被截斷成別的檔案。

## 工具設定

Ghidra MCP 內建的命名檢查與這套慣例衝突，設定檔與已知的殘留警告見 [`tools/ghidra_config/_index.md`](../tools/ghidra_config/_index.md)。
