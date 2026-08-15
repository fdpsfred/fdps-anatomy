# 命名慣例

**驗證對象**：Ghidra 內的 function 與 global 符號名、`src/` 的識別字與檔名。符號怎麼命名的結論以此檔為唯一正典。

慣例沿用前作 FD2（來源：[`docs/research/fd2-playbook.md`](../docs/research/fd2-playbook.md) 轉述的 FD2 `rebuild_info/src_map.md`），只把專案代號換成 `fdps`。

## 鐵則：Ghidra 名稱與 C 名稱逐字相同

同一個東西在 Ghidra 裡叫什麼，在 `src/` 裡就叫什麼，一個字元都不差。這是兩邊能互相對照的唯一基礎，也是 emit 完之後還能回頭查證的前提。

## 符號前綴

| 對象 | 前綴 | 形式 | 例 |
| --- | --- | --- | --- |
| 遊戲邏輯 function | `fdps_` | snake_case | `fdps_rle_blit_sprite`、`fdps_get_item_entry` |
| 全域資料 | `data_fdps_` | snake_case | `data_fdps_item_table` |
| Watcom CRT | `crt_` | 前綴後照抄程式庫符號 | `crt_memcpy`、`crt___ExpandDGROUP` |
| CRT 中必須手寫等價實作的 | `crt_equivalent_` | snake_case | `crt_equivalent_strcmp` |
| Miles AIL | `AIL_` | 保留上游原名，大小寫照舊 | `AIL_startup`、`AIL_set_sequence_loop_count` |

`crt_` 與 `AIL_` 兩類**不套用** `fdps_` 慣例。

`crt_` 之後接的是 Watcom 程式庫 `PUBDEF` 裡的符號原樣，底線與大小寫都不改：`memcpy` → `crt_memcpy`，`__ExpandDGROUP` → `crt___ExpandDGROUP`，`_nmalloc` → `crt__nmalloc`。改寫成 snake_case 會切斷與程式庫的對照關係，而那個對照正是 CRT 判定的證據本身。

**唯一的無前綴豁免是 C 進入點 `main`**——Watcom CRT 的 `cmain386` 契約要求這個符號就叫 `main`。

## pool 分類

每個符號歸 `fdps` / `crt` / `ail` / `binary_artifact` 四個 pool 之一。

**不能用位址範圍判定 pool。** 前作的教訓是三類 function 在同一個 object 裡互相交錯擺放，沒有乾淨的 library／遊戲分界；本專案的 `0x3c000` 分界同樣只是概略值（見 [`program_info/memory_layout.md`](../program_info/memory_layout.md)）。可靠的依據是命名前綴，輔以 caller/callee 關係與 byte 內容。逐一判定的要求見 [ADR-0002](../docs/adr/0002-no-batch-processing-per-function.md)。

## 檔名

`.c` 與 `.h` 的 basename ≤ 8 字元。Watcom 的 DOS 版工具鏈沒有長檔名支援，超過就會被截斷成別的檔案。

## 工具設定

Ghidra MCP 內建的命名檢查與這套慣例衝突，設定檔與已知的殘留警告見 [`tools/ghidra_config/_index.md`](../tools/ghidra_config/_index.md)。
