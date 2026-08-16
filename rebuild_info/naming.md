# 命名慣例

**驗證對象**：Ghidra 內的 function 與 global 符號名、`src/` 的識別字與檔名。符號怎麼命名的結論以此檔為唯一正典。

慣例沿用前作 FD2，正典是 FD2 的 `rebuild_info/src_map.md` 與 `rebuild_info/crt/symbol_inventory.md`，只把專案代號換成 `fdps`。

## 兩條鐵則

**Ghidra 名稱與 C 名稱逐字相同。** 同一個東西在 Ghidra 裡叫什麼，在 `src/` 裡就叫什麼，一個字元都不差。這是兩邊能互相對照的唯一基礎，也是 emit 完之後還能回頭查證的前提。

**程式庫函式就叫程式庫的名字，不加任何前綴。** 名字的用途是讓 wlink 拿去解析真正的 `.LIB`，所以拼法、底線、大小寫逐字照抄：`memcpy`、`_nmalloc`、`__CHK`、`__STKOVERFLOW`、`IF@COS`。`IF@` 這一族的 `@` 是符號的一部分，保留。加上 `crt_` 之類的前綴會讓名字解析不到，而且切斷與程式庫的對照關係——那個對照正是 pool 判定的證據本身。

## 符號前綴

| 對象 | 形式 | 例 |
| --- | --- | --- |
| 遊戲邏輯 function | `fdps_` + snake_case | `fdps_rle_blit_sprite`、`fdps_get_item_entry` |
| 遊戲全域資料 | `data_fdps_` + snake_case | `data_fdps_item_table` |
| Watcom CRT 真符號 | **程式庫原名，無前綴** | `memcpy`、`_nmalloc`、`__CHK`、`IF@COS` |
| 程式庫 object 內的 file-static | `L$N_<模組>_<用途>` | `L$1_stk_save_ss` |
| 行為等價但比對不到程式庫 object、必須手寫的 | `crt_equivalent_` + snake_case | `crt_equivalent_get_eflags` |
| Miles AIL 公開 API | `AIL_` + 上游原名，大小寫照舊 | `AIL_startup`、`AIL_set_sequence_loop_count` |
| AIL 內部 helper 與 inner worker | `AIL_internal_` + 描述，或 `AIL_internal_<公開名>_inner` | `AIL_internal_alloc_and_commit`、`AIL_internal_start_sample_inner` |
| AIL 混音分派表 callback | `AIL_internal_mix_finalize_<兩位十六進位 slot>` / `AIL_internal_mix_loop_<同>` | `AIL_internal_mix_finalize_01` |
| 連結器與編譯器產物 | `binary_artifact_` + 描述 + `_<位址>` | `binary_artifact_align_nop_3d370` |

`crt_`／`AIL_`／`binary_artifact_` 三類**不套用** `fdps_` 慣例。

**唯一的無前綴豁免是 C 進入點 `main`**——Watcom CRT 的 `cmain386` 契約要求這個符號就叫 `main`。

## 兩個容易搞錯的細節

**同一支 helper 被靜態連結兩次時，thunk 用原名，body 加位址後綴。** 廠商的 `.LIB` 裡同一個 helper 可能由兩個 object 各帶一份，連結後成為一個 5-byte `JMP` thunk 加一份完整 body。前作的庫就是這樣命名的（`AIL_internal_log_lock_acquire` 是 thunk，`AIL_internal_log_lock_acquire_3e724` 是 body），照抄。

**名字要是程式庫真的有的符號。** 判定不出 PUBDEF 就不要硬湊一個像 CRT 的名字：那是 file-static（用 `L$N_`）或必須手寫的等價實作（用 `crt_equivalent_`）。可查證的公開符號清單由 [`tools/pool_triage/fid/`](../tools/pool_triage/fid/_index.md) 的 `extract_watcom_symbols.py` 從實際連結的四個程式庫產生。

## pool 分類

每個符號歸 `fdps` / `crt` / `ail` / `binary_artifact` 四個 pool 之一，判定結果與依據見 [`program_info/code_pools.md`](../program_info/code_pools.md)。

**不能用位址範圍判定 pool。** 前作的教訓是三類 function 在同一個 object 裡互相交錯擺放，沒有乾淨的 library／遊戲分界；本專案的 `0x3c000` 分界同樣只是概略值（見 [`program_info/memory_layout.md`](../program_info/memory_layout.md)）。可靠的依據是函式庫比對，輔以 caller/callee 關係與共用資料。逐一判定的要求見 [ADR-0002](../docs/adr/0002-no-batch-processing-per-function.md)。

**名字不是 pool 的證據，pool 也不決定名字。** 兩者各自有判定依據；`0003dc2f` 那個共用 epilogue 帶著 AIL 的名字而形狀像連結產物，就是這條的實例。

## 檔名

`.c` 與 `.h` 的 basename ≤ 8 字元。Watcom 的 DOS 版工具鏈沒有長檔名支援，超過就會被截斷成別的檔案。

## 工具設定

Ghidra MCP 內建的命名檢查與這套慣例衝突，設定檔與已知的殘留警告見 [`tools/ghidra_config/_index.md`](../tools/ghidra_config/_index.md)。
