# FDPS 逆向工程

繁體中文版 DOS 遊戲「炎龍騎士團外傳」（Flame Dragon Plus，簡稱 FDPS）的逆向工程專案。

目標有兩個：從 `FDPS.LE`（362,469 byte 的 32-bit DOS/4G LE 模組）的機械碼還原出完整的 C 原始碼，用當年的 Watcom C/C++ 10.0a 重新編譯出**功能等價**、能在 DOS 下直接取代原版遊玩的執行檔；並把這款遊戲的每個面向——程式怎麼運作、資源檔的格式、數值、每一章的內容、做了卻沒用上的東西——整理成可以對照程式碼與資料查證的知識庫。

功能等價的定義見 [ADR-0001](docs/adr/0001-only-functional-equivalence.md)：外顯行為與功能一致，不要求與原版逐 byte 相同。

## 成果

- **完整的 C 原始碼。** `FDPS.LE` 的 1,345 個 function 逐一判定了歸屬（[`program_info/code_pools.md`](program_info/code_pools.md)）：遊戲自己的 514 個全部還原成 `src/` 的 C（89 個 `.c`、90 個 `.h`），手寫組合語言的鍵盤、存檔與 RLE 繪製保留原版組語（6 個 `.asm`）；Watcom 執行期函式庫從工具鏈的 `.LIB` 連結，Miles AIL 音效庫沿用前作 FD2 抽出重建的 `ailv3.lib`（[`libs/`](libs/_index.md)）。全域資料的初值逐 byte 取自原版映像。
- **可以玩的重建版。** `src/` 連結成取代原版的 `FDE.EXE`，與原版在同一套環境下並排遊玩；開發者已實機玩過前兩章，基本沒有問題。每支 function 另有連結生產程式碼的單元測試（`tests/`）。
- **Ghidra 的分析狀態**：每個 function 的名稱、calling convention、參數名與描述行為的 plate comment，以文字快照進版控（[`ghidra_snapshot/`](ghidra_snapshot/_index.md)）。
- **知識庫**：程式行為、資源格式、數值表、30 章的關卡內容與全文對白、刪減與未用的內容（附圖、音效與文字全文）、重建時會踩的雷，全部對照 `src/`、Ghidra 與遊戲檔逐條驗證過。
- **查詢工具**：`fdps-data` skill 可依名稱、代碼、數值查物品、法術、人物、職業、敵人、商店，查每一章的敵人、寶物、事件與過場腳本，以及 66 個文字區塊的全文與「這條文字由誰顯示、為什麼不會顯示」（資料集由 [`tools/data_skill/`](tools/data_skill/_index.md) 從遊戲檔現解產生）。

## 知識庫怎麼讀

知識庫記錄「結論是什麼」，採結論式寫作；「怎麼走到這個結論」寫在 `devlog/`，兩者分開。每個資料夾有 `_index.md`，說明該資料夾回答什麼問題、索引其中每一份文件；**每個事實只有一個擁有者**，其他地方只放連結。

先決定你的問題屬於哪個視角，再從那個資料夾的 `_index.md` 進去：

| 資料夾 | 回答的問題 |
| --- | --- |
| [`program_info/`](program_info/_index.md) | 程式怎麼運作：啟動與頂層迴圈、記憶體佈局、戰鬥、AI、移動、法術與道具、章節與事件、過場、對話、村莊、存讀檔、CD 音源，以及玩家玩得出來的原版 bug（`known_bugs.md`） |
| [`resource_info/`](resource_info/_index.md) | 每個資源檔的二進位格式：`.VFS` 容器、`.CEL` 圖、`.SAF` 動畫、地圖與地形層、過場腳本、文字區塊、資料表、存檔、兩片光碟的內容 |
| [`assets/`](assets/_index.md) | 遊戲的數值：物品、法術、人物、職業、敵人、種族、商店、譯名；`text/` 是字模對照表、全域文字與額外場景的文字 |
| [`chapters/`](chapters/_index.md) | 每一章：劇情、加入與離隊、勝敗條件與特殊機制、部署與波次、寶物、處理流程、回合與格子事件、過場腳本逐步內容與全文對白；`_index.md` 擁有跨章的事實 |
| [`cut_content/`](cut_content/_index.md) | 做了卻用不到、留了位置卻沒內容的東西，分成殘留內容、空殼、被封住的內容、前作遺留四類（定義見 [`CONTEXT.md`](CONTEXT.md)），附 PNG、WAV 與文字全文；不屬於任何一類的列在排除清單 |
| [`rebuild_info/`](rebuild_info/_index.md) | 怎麼重建：工具鏈與旗標、建置流程、回歸閘、emit 流程、資料落地、原始碼編排、命名、實機驗證，以及「照直覺寫就會與原版不同」的踩雷點總表（`pitfalls.md`） |
| [`libs/`](libs/_index.md) | 重建要連進去、工具鏈不附帶的第三方程式庫 |
| [`ghidra_snapshot/`](ghidra_snapshot/_index.md) | Ghidra 目前的分析狀態的文字快照 |

幾個入口：

- 詞彙（pool、emit、章號與章節索引、地圖編號、刪減與未用的四類……）在 [`CONTEXT.md`](CONTEXT.md)。章號 1 起算、程式內部的章節索引 0 起算，兩者差 1，讀任何表之前先確認手上的是哪一種。
- 找一個數字、代碼、名稱或一句台詞是什麼：用 `fdps-data` skill（`.claude/skills/fdps-data/`）。
- 找一支 function：`src/` 是主要依據（每支都有描述行為的註解），Ghidra 的 plate comment 為輔；`program_info/` 的各頁以 `名稱`（`位址`）標出每條規則出自哪支 function。
- 決策記錄在 [`docs/adr/`](docs/adr/)；攻略站鏡像在 [`docs/guide/`](docs/guide/_index.md)，只做字串搜尋（[`tools/guide_scrape/`](tools/guide_scrape/_index.md) 的 `search`），不當成資料來源解析。
- 當下無法收斂的問題在 [`open_issues.md`](open_issues.md)。

## 建置

建置在 DOSBox-X 內以當年的工具鏈完成，由 Python 腳本全自動驅動。需要：

- Watcom C/C++ 10.0a（`C:\Users\fdpsf\Documents\WATCOM_10_series\WATCOM_10.0a`）
- DOSBox-X（`C:\DOSBox-X`）
- 原始遊戲檔，放在 `fdps_game_files/`（有版權，不進版控）
- 光碟映像 `FDPS_DISC_1.cue`／`FDPS_DISC_2.cue`：遊戲啟動時會檢查光碟，實機遊玩必須掛載

```
python tools/game_build/build_game.py                 # 編譯 src/、連結成 FDE.EXE
python tools/build_gate/gate.py check                 # 完整的回歸閘：重建、與前一版比對、跑全部測試
python tools/game_build/play.py run rebuilt           # 開重建版來玩
python tools/game_build/play.py run original          # 開原版來玩
```

旗標組與判定依據見 [`rebuild_info/build_flags.md`](rebuild_info/build_flags.md)，建置流程見 [`rebuild_info/build_pipeline.md`](rebuild_info/build_pipeline.md)，AIL 的連結契約見 [`rebuild_info/ail_link.md`](rebuild_info/ail_link.md)，腳本的細節見 [`tools/game_build/`](tools/game_build/_index.md)。

## 驗證

- **回歸閘**（[`rebuild_info/build_gate.md`](rebuild_info/build_gate.md)）比的是本專案前一版的建置結果，不是原版：改動只要意外改變了編譯結果就擋下。改註解、改名等不該影響輸出的改動，映像必須逐 byte 相同，或只差在連結器重寫與編譯器不清零的位置。
- **單元測試**連結生產程式碼但不修改它，生產原始碼裡沒有條件編譯的測試 hook（[ADR-0003](docs/adr/0003-manual-playtest-over-automated-golden.md)）；每支 function 要對過哪些檢查見 [`rebuild_info/emit_pipeline.md`](rebuild_info/emit_pipeline.md)。
- **整合行為**由人在 DOSBox-X 中並排遊玩原版與重建版確認，不建自動化對拍（[`rebuild_info/playtest.md`](rebuild_info/playtest.md)）。
- **知識庫**由產生器寫出的部分（字模表、全域文字、資料表、章節頁的產生區塊、刪減與未用的素材）各有閘門逐格對回遊戲資料；人手寫的部分逐條對照 `src/`、Ghidra 與遊戲檔驗證過（[`tools/kb_verify/`](tools/kb_verify/_index.md)）。

## 工作方式

- 逐項的工作（每個 function、每個全域、每份文件、每條痕跡）一律一次一個、由 workflow 全自動跑完（[ADR-0002](docs/adr/0002-no-batch-processing-per-function.md)、[ADR-0007](docs/adr/0007-workflow-automation-and-agent-context.md)）；工作規範在 [`CLAUDE.md`](CLAUDE.md)。
- Ghidra 專案本身不進版控，取而代之的是 `ghidra_snapshot/` 的文字快照（[ADR-0005](docs/adr/0005-ghidra-state-as-versioned-text.md)）。只要動過 Ghidra，commit 之前就跑一次 [`tools/ghidra_snapshot/ExportGhidraSnapshot.java`](tools/ghidra_snapshot/_index.md)：同樣的狀態匯出 byte 相同的結果，commit 裡有 Ghidra 的改變卻沒有快照的 diff，就是漏匯出了。
- 前作 FD2 專案的完整做法整理在 [`docs/research/fd2-playbook.md`](docs/research/fd2-playbook.md)；每項工作開工前先查前作做過沒有。

## 資料夾一覽

| 資料夾 | 內容 | 進版控 |
| --- | --- | --- |
| `src/` | 重建的完整原始碼 | ✓ |
| `tests/` | 單元測試，一個檔鏡像一個 `src/` 檔 | ✓ |
| `program_info/`、`resource_info/`、`assets/`、`chapters/`、`cut_content/`、`rebuild_info/` | 知識庫（見上） | ✓ |
| `libs/` | 重建要連進去的第三方程式庫與標頭 | ✓ |
| `ghidra_snapshot/` | Ghidra 分析狀態的文字快照 | ✓ |
| [`tools/`](tools/_index.md) | 工作腳本，一項工作一個子資料夾 | ✓ |
| `docs/` | 決策記錄、攻略站鏡像、前作研究筆記 | ✓ |
| `.claude/skills/` | `fdps-data` 查詢 skill 與 `ghidra-usage` 操作慣例 | ✓ |
| [`devlog/`](devlog/_conventions.md) | 怎麼走到這些結論的敘事記錄，workflow 的原始回報在 `devlog/runs/` | ✓ |
| [`.scratch/`](.scratch/fdps-rebuild/spec.md) | 專案 spec 與工作票（`fdps-rebuild/issues/`） | ✓ |
| `workspace/` | 腳本的中間產物與輸出，全部可重生，知識庫不引用 | ✗ |
| `legacy/` | 單向封存的舊架構，工作時不讀也不引用 | ✗ |
| `fdps_game_files/` | 原始遊戲檔（版權） | ✗ |
