# ghidra_snapshot — Ghidra 狀態文字快照

把 Ghidra 目前對 `FDPS.LE` 的分析成果匯出成排序穩定的純文字，讓 Ghidra 的變化變成 git 看得到的 diff。這是 [ADR-0005](../../docs/adr/0005-ghidra-state-as-versioned-text.md) 的實作，輸出在 repo 根目錄的 `ghidra_snapshot/`（進版控）。

## `ExportGhidraSnapshot.java`

Ghidra script，透過 Ghidra MCP 執行，參數是輸出目錄（省略時寫到 `ghidra_snapshot/`）：

```
run_ghidra_script(
  script_name="C:/Users/fdpsf/Documents/fdps-anatomy/tools/ghidra_snapshot/ExportGhidraSnapshot.java")
```

整趟約 2 秒，不修改程式。current program 不是 `FDPS.LE` 就直接拋例外，連輸出目錄都不會建立——預設輸出路徑是進版控的，匯出別的程式等於把快照換成另一個 binary 的狀態。所有內容都先在記憶體裡收齊才開始寫檔，避免中途失敗留下新舊混雜的目錄。

## 輸出

一個面向一個檔案，每行一筆記錄，欄位以 `|` 分隔；每個檔案開頭幾行 `#` 註解說明欄位。

| 檔案 | 內容 |
| --- | --- |
| `program.txt` | 程式識別（語言、compiler spec、執行檔 MD5/SHA-256）、memory block、各節計數 |
| `functions.txt` | 每個 function 的位址、body 大小、calling convention、簽章與其來源、stack purge、thunk/noreturn 等旗標、function tag（pool 標記）、custom storage 配置、顯式命名的區域變數 |
| `comments.txt` | plate、pre、post、EOL、repeatable comment 全文 |
| `data_types.txt` | struct、union、enum、typedef、function definition 的完整定義與欄位註解 |
| `labels.txt` | 顯式建立的 label 與其 namespace、來源 |
| `data.txt` | 每一筆 defined data 的位址、大小、套用的型別、主要符號名 |
| `bookmarks.txt` | 全部 bookmark，error bookmark 讓反組譯損壞的修復進度成為 diff |

### 刻意排除的內容

LE loader 從 relocation table 產生的三族產物——`/_le` 底下的 fixup 型別、`fix_off32_<位址>` label、`fixup to -> <位址>` PRE comment——是匯入產物而非分析成果，數量近七千筆，全部排除。它們的筆數記在 `program.txt` 的 `counts.excluded.*`，所以數量變動仍然看得見。

自動命名的符號（`DAT_`、`LAB_`）與從內建型別衍生的 pointer、array 不帶任何判斷，也不列出；`data.txt` 仍逐筆列出 defined data，因此對某個位址套用型別或改名都會顯示成 diff。

### 決定性

同樣的 Ghidra 狀態重複匯出得到 byte 相同的結果：所有集合都顯式排序，輸出不含時間戳，檔案一律 UTF-8、LF 換行，comment 內的 CRLF 正規化成 LF。
