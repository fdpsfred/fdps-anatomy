# ghidra_config — Ghidra MCP 的專案設定

Ghidra 專案本身不進版控（[ADR-0005](../../docs/adr/0005-ghidra-state-as-versioned-text.md)），但它的設定要可重建。這裡放正本，安裝時複製過去。

| 檔案 | 安裝位置 |
| --- | --- |
| `conventions.json` | `C:\Users\fdpsf\Documents\reverse_fdps\reverse_fdps.rep\.ghidra-mcp\conventions.json` |

```powershell
Copy-Item tools\ghidra_config\conventions.json `
  "C:\Users\fdpsf\Documents\reverse_fdps\reverse_fdps.rep\.ghidra-mcp\conventions.json" -Force
```

設定檔在 plugin 啟動時讀入。改完之後要重啟 Ghidra，或在 `Edit ▸ Tool Options ▸ GhidraMCP ▸ Strict Naming Enforcement` 把開關切掉再切回來，才會重新載入。

## 為什麼要改預設值

Ghidra MCP 內建的命名檢查預設 PascalCase 函式名、全域變數強制 `g_` 前綴、struct 欄位自動改寫成匈牙利命名。本專案沿用前作 FD2 的慣例——`fdps_` 前綴加 snake_case、全域是 `data_fdps_` 前綴、vendor 用 `crt_` 與 `AIL_`——與預設值直接衝突。

| 設定 | 值 | 理由 |
| --- | --- | --- |
| `strict_mode` | `warn` | 寫入照樣成立，檢查結果只當提示。命名是逐一判定後才寫的，不需要工具擋 |
| `function_naming.min_length` | 4 | C 進入點 `main` 是唯一不加前綴的名字，預設的 8 會擋掉它 |
| `hungarian.auto_fix_struct_fields` | `false` | 欄位名照給的寫，不要被改寫成 `dwCount` 這種形式 |
| `global_naming.require_g_prefix` | `false` | 全域的前綴是 `data_fdps_`，不是 `g_` |
| `plate_comments.required_sections` | `Algorithm` / `Parameters` / `Returns` | 與 `.claude/skills/ghidra-usage/PLATE_COMMENT_EXAMPLES.md` 的格式一致 |

## 已知的殘留噪音

即使套用這份設定，每次把 function 改成 snake_case 名稱時仍會回傳兩則警告，說名字不是 PascalCase、不該有底線。大小寫規則是寫死的，設定檔沒有對應的開關；要完全消掉只能把 `strict_mode` 設成 `off`，那會連 plate comment 與全域命名的檢查一起關掉，不划算。

**這兩則警告是預期的，照本專案的慣例寫就一定會出現，不要為了消除它們把名字改成 PascalCase。**
