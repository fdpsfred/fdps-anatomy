# FDPS 逆向工程

繁體中文版 DOS 遊戲「炎龍騎士團外傳」(Flame Dragon Plus，簡稱 FDPS) 的逆向工程專案。

目標是從 `FDPS.LE`（362,469 byte 的 32-bit DOS/4G LE 模組）的機械碼還原出完整的 C 原始碼，用 Watcom C/C++ 10.0a 重新編譯出**功能等價**的執行檔，能在 DOS 環境下直接取代原始執行檔遊玩；同時把這款遊戲的每個面向整理成可查證的事實。

功能等價的定義見 [ADR-0001](docs/adr/0001-only-functional-equivalence.md)：外顯行為與功能一致，不要求 binary byte 相同。

## 知識庫結構

知識庫記錄「結論是什麼」，採結論式寫作。每個資料夾有 `_index.md`，說明該資料夾的職責並索引內容；跨檔的事實只寫在 `_index.md`，格式與數值細節只寫在該主題的正典檔——**每個事實只有一個擁有者**。

| 資料夾 | 視角 | 回答的問題 | git |
| --- | --- | --- | --- |
| `src/` | — | 逆向重建的完整 C 原始碼，解析遊戲資訊時的主要依據，Ghidra 為輔 | ✓ |
| [`program_info/`](program_info/_index.md) | 程式 | `FDPS.LE` 現在做什麼，一檔對應一個子系統與 `src/` 模組 | ✓ |
| [`resource_info/`](resource_info/_index.md) | 檔案 | 每個資源檔的二進位格式是什麼 | ✓ |
| [`assets/`](assets/_index.md) | 資料表 | 遊戲的數值內容是什麼 | ✓ |
| [`chapters/`](chapters/_index.md) | 關卡 | 每一章的關卡內容與事件流程是什麼 | ✓ |
| [`rebuild_info/`](rebuild_info/_index.md) | 重建 | 怎麼重建成等價執行檔、哪裡會踩雷 | ✓ |
| [`ghidra_snapshot/`](ghidra_snapshot/_index.md) | Ghidra | Ghidra 目前的分析狀態，以文字快照進版控 | ✓ |
| [`tools/`](tools/_index.md) | — | 工作腳本，一項工作一個子資料夾 | ✓ |
| [`docs/`](docs/) | — | 決策記錄（[`adr/`](docs/adr/)）、攻略站鏡像（[`guide/`](docs/guide/_index.md)）、agent 規範、前作研究筆記 | ✓ |
| `.claude/skills/` | — | skill：`fdps-data` 查遊戲數值，`ghidra-usage` 是 Ghidra 操作慣例 | ✓ |
| [`devlog/`](devlog/_conventions.md) | — | 怎麼走到這些結論的敘事記錄 | ✓ |
| [`.scratch/`](.scratch/fdps-rebuild/spec.md) | — | 專案 spec 與工作票（`fdps-rebuild/issues/`） | ✓ |
| `workspace/` | — | 腳本的中間產物與輸出，全部可重生 | ✗ |
| `legacy/` | — | 單向封存的舊架構，工作時不得閱讀或引用 | ✗ |
| `fdps_game_files/` | — | 原始遊戲檔（版權），repo 不含 | ✗ |

## Ghidra 快照的匯出時機

Ghidra 專案本身不進版控，取而代之的是 `ghidra_snapshot/` 這份文字快照（[ADR-0005](docs/adr/0005-ghidra-state-as-versioned-text.md)）。匯出與 commit 綁定：每個工作段落結束、要 commit 之前，先跑一次 [`tools/ghidra_snapshot/ExportGhidraSnapshot.java`](tools/ghidra_snapshot/_index.md)，把重新匯出的快照與程式碼、知識庫、devlog 放進同一個 commit。

只要動過 Ghidra 就重跑一次匯出——快照對同樣的狀態會產生 byte 相同的輸出，狀態沒變就不會有 diff，多跑沒有代價。反過來說，commit 裡出現 Ghidra 的行為改變卻沒有對應的快照 diff，就是漏匯出了。

其他入口：[`CONTEXT.md`](CONTEXT.md) 是詞彙表，[`CLAUDE.md`](CLAUDE.md) 是工作規範，[`open_issues.md`](open_issues.md) 是當下收不了、要等更後面階段才有材料回答的問題，[`docs/research/fd2-playbook.md`](docs/research/fd2-playbook.md) 是前作 FD2 專案的完整 playbook。

## 建置

建置在 DOSBox-X 內以當年的工具鏈完成，需要：

- Watcom C/C++ 10.0a（`C:\Users\fdpsf\Documents\WATCOM_10_series\WATCOM_10.0a`）
- DOSBox-X（`C:\DOSBox-X`），自動化時以 `-silent` 執行
- 光碟映像 `FDPS_DISC_1.cue` / `FDPS_DISC_2.cue`——遊戲啟動時會檢查光碟，建置後的實機驗證必須掛載
- 原始遊戲檔（`fdps_game_files/`，不進版控）

建置腳本、連結參數與 vendor library 的接法由 `rebuild_info/` 記載。

## 驗證

- 單元測試連結生產程式碼但不修改它，生產原始碼中不得有條件編譯的測試 hook（[ADR-0003](docs/adr/0003-manual-playtest-over-automated-golden.md)）
- Build gate 是與本專案前一版建置結果比較的自我回歸閘，不是與原版的等價性證明
- 解析器先跑前作的站台與資源檔，對照前作知識庫的已知答案驗證
- 整合行為由人在 DOSBox-X 中實際遊玩確認，不建自動化對拍設施
