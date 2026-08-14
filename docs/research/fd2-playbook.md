# FD2 逆向工程專案 Playbook（給 FDPS 姊妹專案）

> 本文件是對 `fd2-anatomy`（炎龍騎士團二代 FD2 逆向工程專案，已完成）的一手資料研究結論，
> 供 `fdps-anatomy`（炎龍騎士團外傳 FDPS）從零開始時參考。
> 所有結論皆標註來源檔案路徑（相對於 `fd2-anatomy/`）或 commit hash。
> 本研究為純讀取，未修改 `fd2-anatomy` 任何檔案。

---

## TL;DR：新專案該怎麼開始

FD2 專案的實際順序是「**先在 Ghidra 把整個 binary 分類乾淨 → 再談重建**」，中間沒有捷徑。
FDPS 建議照同樣順序，並在最前面插入一個 FD2 沒有的步驟：**先確認 FDPS 的 toolchain 是否與 FD2 相同**。

1. **Step 0（FD2 沒有、FDPS 必做）— toolchain 指紋比對。**
   先判定 FDPS.LE 的編譯器版本與連結的 CRT lib。FD2 的做法是跨版本 `.obj` SHA256 比對 + Ghidra FidDb，
   把版本收斂到唯一相容的 Watcom C/C++ 9.5a（來源：`rebuild_info/crt/fid_match.md:39-47`）。
   **注意：FD2 專案一開始把編譯器誤判成 Borland C++，第 3 天才更正**（來源：commit `6c0bed80`），
   之後又花了兩週才把版本收斂到 9.5a（來源：commit `3f42055d`）。這是整個專案最貴的一次返工，FDPS 應該提前做。
2. **Phase A — Ghidra 全 binary 分類（FD2 花約 3.5 週）。**
   目標：所有 function 被定位、命名、calling convention 正確、四 pool（fd2 / crt / ail / binary_artifact）分類完成、
   orphan code / jump table / fall-through 全部處理掉、call graph 建出來。
   完成判定＝「0 個 `FUN_*` / `vendor_*` 殘留名 + 0 error bookmark」（來源：`rebuild_info/equivalence/rules.md:266-271`、`CLAUDE.md:35-40`）。
3. **Phase B — vendor lib 抽取（AIL）。** 把 Miles AIL audio library 從 binary 抽成可連結的 `.lib` + `.h`（來源：commit `19b783d8`、`tools/ail_extract/_index.md`）。
4. **Phase C — function emit（逐函式 Ghidra → C，FD2 共 651 個）。** per-function：emit → 單元測試 → build gate → reviewer 復驗 → 單獨 commit（來源：`tools/code_emit/_index.md:8-26`；實測 `routing.json` 651 筆，done/reviewed 皆 651）。
5. **Phase D — data emit（全域資料真實 byte → C initializer）。** 以 linker 的 undefined symbol 清單當權威 worklist（來源：`rebuild_info/build_test/workflow.md:30-34`）。
6. **Phase E — 連結成 EXE + 實機 playtest 修 bug。** 這是最兇的階段：8 類「編譯全綠、單元測試全綠，但在真機上行為偏離原版」的 bug（來源：`rebuild_info/build_test/playtest_bugs.md:23-33`）。
7. **Phase F — 決定論 playthrough golden 測試 + 原版差分背書。**（來源：`rebuild_info/verification.md:41-73`）
8. **Phase G — src_refine（逐符號改名 / 補註解）+ 知識庫翻新 + 發佈。**（來源：`tools/src_refine/_index.md`、commit `2bfa777e`）

**三件最該先知道的事**：

- **等價目標是 Layer 2（functionally-exact），不是 byte-exact。** README 講的「100% 復刻」指的是外顯行為一致，
  不是重建 EXE 與原版 EXE byte 相同（來源：`rebuild_info/equivalence/rules.md:38-43`）。
- **`src/` 一旦能編譯，它就取代 Ghidra 成為主要資訊來源**；之後任何改動都必須過 `eqcheck.py` / `hash_check.py` 二選一的 gate（來源：`CLAUDE.md:8-9`）。
- **會咬人的 bug 不在「算錯」，而在「隱性契約」**：暫存器 clobber、資料相鄰性、號性、熱迴圈指令數、絕對位址、
  intrinsic 呼叫形式、stack-probe 分佈、fixup 綁錯符號（來源：`rebuild_info/build_test/playtest_bugs.md:249-258`）。

---

## 0. FD2 專案速覽（規模數字，供 FDPS 估算）

| 項目 | 數字 | 來源 |
|---|---|---|
| git commit 總數 | 2338 | `git log --oneline \| wc -l` |
| 專案期間 | 2026-05-02 ～ 2026-07-19（約 11 週，實際活躍約 30 天） | `git log --reverse --date=short` |
| FD2.LE 大小 | 約 346,650 byte（LE 模組本體，不含 stub） | `rebuild_info/link/le_layout.md:9` |
| binary 內 function 總數 | 1342（端到端 re-review 過） | `open_issues.md:96` |
| 需 emit 成 C 的 function | 651 | `tools/code_emit/data/routing.json`（實測筆數） |
| data symbol worklist | 347 分類（其中 153 需要真實 byte） | commit `ad828a6f` |
| 重建 `src/` | 64 個 `.c`、51,115 行、15 個子系統目錄 | 實測 `find src -name '*.c'` |
| CRT function 識別 | 194 筆進 `lookup_9.5a.json` | `rebuild_info/crt/fid_match.md:8` |
| AIL function | 422 個分類 | `open_issues.md:100` |
| 實機 playtest 修掉的 bug | 8 類 | `rebuild_info/build_test/playtest_bugs.md:23-33` |

---

## 1. 工作流程與階段劃分

git history 顯示的實際階段（每階段的產出與「完成」判定如下）。
注意：**第一個 commit `381fc48f`（2026-05-02）就已經包含 200 個檔案、44,631 行**的初版知識庫
（`program_info/`、`resource_info/`、`assets/`、`chapters/`、`tools/decoders/`、`tools/glyph/`，
以及一個從攻略本建出來的 `.claude/skills/fd2-knowledge/` 查詢 skill），
表示「純資源檔／攻略資料的 KB 第一輪」是在版本控制之前就完成的（來源：`git show --stat 381fc48f`）。
**FDPS 若沒有現成的社群攻略資料，這一塊要自己補，且應該當作 Phase 0 獨立處理。**

### Phase 1：Ghidra 全 binary 整理（2026-05-02 ～ 05-25）

| commit | 里程碑 | 產出 |
|---|---|---|
| `381fc48f` | 初始知識庫（資源檔格式 + 攻略資料 + decoders） | `program_info/`、`resource_info/`、`assets/`、`tools/decoders/` |
| `518f2887`、`cda4b9e1` | 全 binary calling convention 修正 | Ghidra function signature |
| `6c0bed80` | **修正編譯器判定：Watcom 而非 Borland C++** | KB 全面改寫 |
| `b705ce59` | 所有 function 定位完成 + signature/內容 review + call graph | `program_info/call_graph.{json,dot,md}`、`tools/function_review/` |
| `81bf4411`、`c37bddaa`、`eab03d97` | CRT function 識別 → lookup table → callee-driven 精修 | `rebuild_info/crt/lookup_9.5a.json` |
| `62b67ec9` | orphan code segment 修復 | Ghidra 完整化 |
| `eb104490` | AIL function audit | `rebuild_info/ail/inventory.md` |
| `3f42055d` | **版本收斂到 Watcom 9.5a** | `rebuild_info/crt/fid_match.md` |
| `19b783d8` | AIL library 抽取完成 | `libs/ailv3/ailv3.lib` + `ailv3.h` |

**完成判定**：全 binary 每個 instruction byte 都歸屬於某個 function 或 align fill；
四 pool 分類完成；0 error bookmark（`CLAUDE.md:35-40`）；
`rebuild_info/equivalence/pool_classification.md:82-86` 明述這條不變式。

### Phase 2：function emit + review（2026-06-01 ～ 06-12）

- `4e1c71c5`（06-01）建立 emit 骨架 + build gate；`0cf820fe` 建立 emit-review workflow 編排。
- 之後是 **每個 function 一個 commit**，commit message 格式固定：
  `emit-review: <fn_name> @ <8-hex address> (review, reviewed-approved)`（例：commit `c381ea3c`）。
- `28e1cd61`（06-04）導入 **worktree 隔離的 parallel emit**：把 function 依 `.c` 檔切成 file-disjoint 分區，
  4 條並行分支各自 build。
- `588d8a6f` / `98ea47b5` / `bed6602d`（06-12）三段 merge cascade 把並行分支合回 `integ`。
  **merge gate 只看「compile + link 零 error 零 warning」**，不看 run 階段（來源：commit `b6d7f130`、`tools/code_emit/_index.md:14`）。

**完成判定**：`routing.json` 每筆 `done=true` 且 `reviewed=true`（來源：`tools/code_emit/_index.md:21-23`）。

### Phase 2.5 / Step 0：資料符號盤點（2026-06-13 ～ 06-14）

- `95e6e648` 建立「`fd2.lnk` 神諭 + data worklist 工具」；`28e8f2e5` 完成 341 符號的 home-file map。
- **關鍵手法**：src-only 連結不過時，linker 回報的 undefined symbol 清單就是「`src/` 還缺什麼」的權威 worklist
  （來源：`rebuild_info/build_test/workflow.md:30-34`）。

### Phase 3：data emit（2026-06-14 ～ 06-16）

- 每個 data symbol 一個 commit：`data-emit: <symbol> (<home .c>, reviewed)`（例：commit `1b00d141`）。
- 每個 home file 完成後跑一次 build gate，失敗則補 `build-gate: <file> (fixes)` commit（例：commit `1a99c6c2`）。
- **里程碑 `17e7a8d6`（06-16）：`src/` 連結出 FD2.EXE，0 undefined。**

### Phase 4：實機 playtest 除錯（2026-06-17 ～ 07-08）

這是最痛的階段，也是最值得預先讀的一段。修掉的 bug（每個都由使用者實機確認）：

| commit | 症狀 | 根因類別 |
|---|---|---|
| `4ca8ad0e` | 鍵盤失效 + sfx flag 被清零 | B（`union REGS` 被拆散） |
| `f44a0a11` | 開場 "File not found" 退出 | E（硬編絕對位址） |
| `e8dc10e0` | SFX 全靜音 | A（AIL clobber pragma） |
| `a9b772ea` | 開場 scene 跳到地圖底部 | C（號性 uint32 vs int） |
| `329ca2ab` | 炙焰刀施法平移 crash | B（三 scalar 應為 array） |
| `a1956966` | 商店腳步聲被對話音效切斷 | D（熱迴圈指令數） |
| `395221d7` | 音效初始化 "Stack Overflow!" | G（stack-probe 分佈） |
| `15d32073` | 白光柱特效在 86Box-macOS runaway page fault | F（math intrinsic 呼叫形式） |
| `0c3ffe12` | 教會復活扣款金額與清單不符 | H（折疊基底歸錯符號） |

（來源：`rebuild_info/build_test/playtest_bugs.md:23-33`）

**分工鐵則（使用者訂定）**：聽音效內容的驗證由使用者跑（AI 不能代聽）；看數值 / 看畫面的診斷由 AI 做；
build/link 由 AI 做（來源：`rebuild_info/build_test/playtest_bugs.md:13-20`）。

### Phase 5：決定論測試系統（2026-06-21 ～ 06-25）

- `9eda3405`（P0 walking skeleton）→ `825fcef5`（P1 + 虛擬時鐘 + fb2png）→ `86815a27`（P2 三十章 init sweep，28/30 乾淨）
  → `fe72d84b`（P3/P4）→ `9dc4dc39`（P3-E 傷害 oracle）。
- `41e8d64e`（06-25）把舊的 per-function spy 單元測試整批退役到 `legacy/`——
  因為真 function body emit 之後 spy 會與真 body 產生 Watcom `W1027` redefinition（來源：`tests/_index.md`）。

### Phase 6：src_refine（2026-06-21 ～ 06-24，約 1300+ commit）

- 逐符號（function + global）分析用途、refine 名稱與註解、同步回 Ghidra plate。
- commit 格式：`src-refine: <symbol> @ <addr> (comment keep|augment|rewrite)`（例：commit `d8989890`）。
- Stage 1（只改註解）gate＝`hash_check.py` byte-identical；Stage 2（改名）gate＝`eqcheck.py` 功能等價
  （來源：`tools/src_refine/_index.md:5-6, 16-17`）。

### Phase 7：知識庫翻新 + 發佈（2026-07-06 ～ 07-19）

- `32380cdf` 起 `kb-overhaul` batch1~batch7f，最後 `2bfa777e`「KB 翻新完工」。
  batch7 是**逐檔逐條機械驗證**（rebuild_info / program_info / resource_info / chapters / assets 各一輪），
  每輪都 patch 出數十處 factual drift（例：commit `107480d7` patch 43 drift）。
- `f2653460` 建 public README；`a408cc11` 建 `tools/publish/` 一鍵發佈到 public repo；
  `72f977dc` 去識別化本機路徑。
- `0c3ffe12` / `505f555b`（07-19）是收尾後又發現的一個真 bug 與其對應的靜態掃描器——
  **代表「完工」之後仍會冒出新問題**。

---

## 2. 知識庫組織

### 頂層資料夾職責（來源：`README.md:117-135`）

| Folder | 職責 | git 追蹤 |
|---|---|---|
| `src/` | 逆向重建的完整 C 原始碼，**解析遊戲資訊時的主要依據**，Ghidra 為輔 | ✓ |
| `program_info/` | 「程式視角」：每個子系統做什麼，一檔對應一個 `src/` 模組 | ✓ |
| `resource_info/` | 「檔案視角」：每個資源檔的二進位格式，一檔對應一種格式 | ✓ |
| `assets/` | 「玩家視角」：從程式與資源檔解出的遊戲數值內容 | ✓ |
| `chapters/` | 每章一檔（30 章），跨章機制放 `_index.md` | ✓ |
| `rebuild_info/` | 重建成等價執行檔所需的 toolchain / lib / ABI / 等價鐵則 / 踩雷紀錄 | ✓ |
| `libs/` | 重建連結需要的 vendor lib（`ailv3.lib` + header） | ✓ |
| `tests/` | 決定論 playthrough 整合測試 + 凍結的邏輯回歸網 | ✓ |
| `tools/` | 可重複利用的 Python script，一個工作一個子資料夾 | ✓ |
| `docs/` | **GitHub Pages 發佈目錄，不是知識庫** | ✓ |
| `workspace/` | 真 scratch，pipeline 中間產物與一次性 POC | ✗（gitignore） |
| `legacy/` | 凍結的舊架構，**工作時絕對不能閱讀和參考** | ✗（gitignore） |
| `fd2_game_files/` | 原始遊戲檔（版權），repo 不含 | ✗（gitignore） |

`rebuild_info/_index.md:6-7` 對 program_info 與 rebuild_info 的分工有一句很精準的定義：
「`program_info/` 回答『FD2 現在做什麼』，本資料夾回答『怎麼把它重建成等價執行檔、以及重建時哪裡會踩雷』。」

`assets/_index.md:37-40` 則定義了三視角切分：`assets/` = 玩家視角、`program_info/` = 程式視角、`resource_info/` = 檔案視角。

### `_index.md` 的用法與維護規範

**核心規範（`CLAUDE.md:19-24`，建議 FDPS 逐字沿用）**：

1. 工作過程中得到的**確定結論**都要整合進知識庫，並確定沒有重複。
2. 知識庫內容必須是「**最後的結論**」，不能是描述分析過程的流水帳。
3. 知識庫內容**不能引用 `legacy/` 或 `workspace/`** 底下的東西。
4. **任何文件和 script 的更新都要反映到對應的 `_index.md`**。
5. 改完文件要再 review 一次，確認沒有「在某個時間解出、以前原本是甚麼、phase」這類流水帳內容，有就立刻修正。

**各 `_index.md` 的實際形態**：

- `program_info/_index.md` 是**「文件 ↔ src 模組 ↔ 內容」三欄表**；每個 doc 開頭有「驗證對象」段，
  列出對應 `src/` 檔、關鍵 Ghidra 符號（`name@address`）與相關資源檔，讓每條主張都能回頭對 Ghidra 驗證。
- `resource_info/_index.md` 是**平鋪清單**，一行一種檔案格式，並宣告每個檔是該編碼的「唯一正典」——
  codec/opcode 表只寫在一個地方，絕不重複。
- `chapters/_index.md` 是**跨章事實的唯一擁有者**：30 章 handler 總表、共用 handler 群組、
  隱藏機制鏈（結局分歧、招募矩陣）、章號 ↔ 資源 entry index 對照表。每章檔案只寫該章內容，不重複索引裡的東西。
- `tools/_index.md` 除了列每個子資料夾用途外，還訂了**儲存慣例**：
  `tools/{工具}/data/` 放「script 無法重生的一手輸入」（外部快照、人工判定），
  `workspace/{工具}/` 放所有可重生的中間產物（`tools/_index.md:29-38`）。
- `tests/play/_index.md` 用 **P0–P6 階段狀態表（✅/⬜ + 佐證）** 追蹤測試覆蓋進度。

**「單一擁有者」原則**貫穿整個 KB：跨檔事實只寫在 `_index.md`，格式細節只寫在該格式的 `.md`，
數值只寫在 `assets/` 的正典檔（`tables/` 只放 struct 並引用之，來源：`assets/_index.md:9`）。

### `legacy/` 的用法

`legacy/` 是**單向封存**：當某套 KB 架構或測試策略被取代，整批搬進 `legacy/`，
並硬性禁止新工作閱讀或引用（`README.md:134-135`：「裡面的所有內容都已過時，工作時絕對不能閱讀和參考」）。
實際內容包括：一套已廢棄的 phase 0–15 Ghidra 標註 pipeline、舊 catalog JSON、舊 ground_truth 散文、
退役的 spy 單元測試（`legacy/tests_unit_spy/`）、work logs。

### `workspace/` 的用法

`workspace/{工作名稱}/` 一個工作一個子資料夾，命名對應產生它的 `tools/{工作名稱}/`。
**KB / tool script / `_index.md` 都不能引用 `workspace/` path**（`README.md:133`）。
實測 `workspace/` 有 16,223 個檔案、70+ 子資料夾——這是這種 pipeline 的正常量體，全部不進 git。

### `.claude/` 的內容

- `.claude/agents/` 在 FD2 是**空的**（沒有 subagent 定義）。
- `.claude/skills/` 四個 skill：
  - `ghidra-usage` — binary-agnostic 的 Ghidra MCP workflow 集（function 文件化 7 步、orphan code 探索、
    struct/型別調查、global 命名、string 標記分類、跨版本 function 比對、命名慣例可用 `conventions.json` 客製）。
    **FDPS 已經有一份**（`fdps-anatomy/.claude/skills/ghidra-usage/`），但兩份內容不同：
    FD2 版多出 `BINARY_DOCUMENTATION_ORDER.md`、`CROSS_VERSION_FUNCTION_MATCHING.md`、
    `CROSS_VERSION_MATCHING_COMPREHENSIVE.md`、`DATA_TYPE_INVESTIGATION_WORKFLOW.md`；
    FDPS 版多出 `FUNCTION_DOC_WORKFLOW_V5_BATCH.md`、`PLATE_COMMENT_EXAMPLES.md`、`QUICK_START_PROMPT.md`，
    其餘同名檔內容也有差異（實測 `diff -rq`）。**跨版本比對那三份對 FDPS 特別有價值**（見 §6）。
  - `fd2-knowledge` — 把攻略本資料建成可查詢索引（`python query.py <subcommand>`），
    讓 agent 能用位址 / struct stride / ID 反查已知遊戲資料。內含**版本漂移警告**：
    攻略記的位址未必等於這份 build 的 Ghidra 位址，byte signature 才是可靠的跨版本錨點，ID 與 struct 大小才穩定。
  - `context-usage`、`anthropic_agent_sdk` — 與專案無關的通用工具 / 參考。

### `src/` 的組織與命名規則（來源：`rebuild_info/src_map.md:8-16`）

| 規則 | 內容 |
|---|---|
| 8.3 檔名 | Watcom 9.5a 無 LFN，所有 `.c`/`.h` basename ≤ 8 字元 |
| game-logic 前綴 | 一律 `fd2_`；**唯一豁免是 C 進入點 `main`**（CRT `cmain386` 契約要求） |
| global data 前綴 | 一律 `data_fd2_`（Ghidra 與 C 端 byte-identical 同名） |
| vendor 前綴 | `crt_` / `crt_equivalent_` = Watcom CRT 層；`AIL_` = Miles AIL，兩類不套用 `fd2_` 慣例 |
| pool 分類 | 每個 symbol 歸 `ail` / `crt` / `fd2` / `binary_artifact` 四 pool 之一 |

`src/` 依子系統切成 15 個目錄（anim / audio / battle / crt / dialog / field / gfx / input / life / rsrc / save /
spell / table / ui_menu / util）+ `src/include/`（`types.h` / `globals.h` / `protos.h` / `consts.h` / `crtcomp.h`）。
`table/` 模組特別：同一批 `.c` 同時放大型 `data_fd2_*` 資料表定義與其薄 accessor（`fd2_get_*_entry`）。
`src/` 本身沒有 `_index.md`，導航件是 `rebuild_info/src_map.md`。

### `.gitignore`（來源：`fd2-anatomy/.gitignore`）

```
legacy/
workspace/
fd2_game_files/
**/__pycache__/
.claude/settings.local.json

# emit pipeline build artifacts (DOSBox-X compile/link/run output)
tests/OUT/
src/DIAG.TXT
src/TEST.OUT
```

`.claude/skills/` 與 `.claude/agents/` **有進版控**，只有 local settings 被排除。

---

## 3. tools/ 腳本清單

共通鐵則（`tools/_index.md:3-4, 29-38`）：每個 script **self-contained**（不 import 共用 lib、
不依賴 `legacy/`、自帶路徑常數），CLI 用 `python <script> --help` 探索；
輸入放 `tools/{工具}/data/`、輸出放 `workspace/{工具}/`。

| 子資料夾 | 解決什麼問題 | 關鍵腳本（輸入 → 輸出） |
|---|---|---|
| `program_analysis/` | FD2.LE 結構性 audit 總成 | `build_call_graph.py`（Ghidra MCP dump → 四 pool 分類 → `workspace/call_graph/{json,dot,md}`；`categorise()`/`emit_action_for()` 是分類的 source of truth）；子夾 `crt_fid_match/`（FidDb 版本判定）、`crt_callee_match/`（byte-level FIXUPP-aware 比對）、`function_audit/`（G1–G9）、`data_audit/`（D1–D12）、`jump_table_audit/` |
| `ail_extract/` | 把 Miles AIL 從 binary 抽成可連結 lib | 8 階段：`dump_ghidra_supplements.py` → `dump_ail_set.py` → `extract_ail_bytes.py` → `enumerate_pcrel32_sites.py` →（**人工判定 1408 + 244 筆 fixup verdict，不可機器重生**）→ `bin_to_omf.py`(+`omf_writer.py`) → `pack_libs.py`（DOSBox 內 wlib）→ `gen_ailv3_h.py`（產生帶 clobber pragma 的 header）→ `run_test.py` |
| `code_emit/` | function emit + review 編排 | `emit_review.wf.js`（雙模式編排：emit / review → reviewer → 迭代 → per-function commit）、`coland.wf.js`（coordinated landing）、`build_test.py`（**唯一 build gate**）、`next_batch.py`（scout 下一批，`--stats` 看覆蓋率）；狀態在 `data/routing.json` |
| `data_emit/` | 全域資料真實 byte → C initializer | `reconcile.py`（Ghidra dump vs `src/` → `worklist.tsv`）、`verify_real.py`（**byte-equality gate**：Ghidra `read_memory` vs 解析出的 C initializer）、`rename_global.py`、`data_emit.wf.js` |
| `fd2_build/` | **production build**（見下） | `build_fd2.py`、`analyze_undefined.py` |
| `src_refine/` | 逐符號 refine + 等價 gate（見下） | `build_worklist.py`、`partition.py`、`scout.py`、`src_refine.wf.js`、`hash_check.py`、`eqcheck.py`、`merge_shards.py` |
| `fd2_play/` | 決定論 playthrough 測試 | `build_replay.py`（`-DFD2_REPLAY` + guest harness → `FD2RP.EXE`）、`run_play.py`、`compare.py`（framebuffer Hamming + state byte-equality，`--bless` 凍 golden）、`fb2png.py`（自帶 zlib PNG encoder，無 PIL 依賴）、`expect.py`（傷害 oracle，同一條 LFSR + 公式）、`gen_scenario.py`（存檔編解碼：XOR involution + checksum）、`sweep_chapters.py`、`run_all.py` |
| `fd2_diff/` | 用**原版**執行結果當正解 | `extract_state.py`（從 DOSBox-X save-state 找 DGROUP 簽章 → 讀 16 個 int32 全域，layout 與 `capture.c` 的 `STnn` 相同）、`make_pro_obj.py`（手寫 OMF obj 提供 `__PRO` hook 給 `-ep` 做覆蓋率） |
| `decoders/` | 資源檔解碼（被其他工具當 library 引用） | `dat_header_parser.py`（LLLLLL archive 通用 header）、`rle_decoder.py`（與 `fd2_rle_blit_sprite` 對齊的 4-op RLE）、各檔專用 decoder |
| `rsrc_unresolved/` | 資源格式 ground-truth 回歸 | `analyze.py`、`verify_dead.py`（窮舉 `src/` 內所有載入點判定 dead resource） |
| `glyph/` | 中文字模對照 | `render_glyph_atlas.py`（1824 個 16×16 1bpp glyph → PNG atlas + CSV）、`et3_pixel_match.py`（對 ET3 字型做 Hamming 比對推 Big5） |
| `oob_index_audit/` | **靜態掃描越界索引**（H 類 bug 的掃描器） | `scan_oob_index.py`（掃 `src/` 內所有 `data_fd2_*` 陣列下標，分類 `OOB_CONST`/`OOB_OFFSET`/`NEAR_END`/…） |
| `chevt_audit/` | 章節事件 handler 存活性稽核 | `enum_live_consequence.py` |
| `kb_overhaul/` | KB 內容機械重生 | `gen_ch_encounters.py`、`gen_ch_section3.py`、`gen_ch_shops.py` |
| `growth_table/` | 數值表 → 互動網頁 | `gen_growth.py` → `build_page.py`（注入 `page_template.html`）→ `docs/` GitHub Pages |
| `snd_kbd_diag/` | 音效 / 鍵盤實機診斷 | `lib_probe.py`（host 版 WLIB/WDISASM dump lib）、`sfxdiag.c`+`run_sfxdiag.py`、`run_fd2_audbg.py` |
| `stkdiag/` | stack-probe 誤判診斷 | `gen_stkdiag.py`（手寫機器碼 OBJ 取代 CLIB3S 的 `__CHK`/`__GRO`/`__STK`，印出 `PROBE=/ESP=`） |
| `publish/` | dev repo → public repo | `publish_public.py`（`git archive` HEAD → 移除 `.claude/`+`CLAUDE.md`+自己 → 掃個資 → 單一全新 commit；預設 dry-run，`--push` 才推） |

### `tools/fd2_build/build_fd2.py` 詳解

**用途**：只用 `src/` + vendor lib 編出 production `FD2.EXE`，零 `tests/` 依賴。

- **DOSBox-X 掛載**：產生獨立的 `fd2build.conf`，掛 4 個磁碟——
  `C:`→`src/`、`E:`→`workspace/fd2_build/exe`（隔離輸出）、`D:`→`%WATCOM%`（預設 `~/Documents/WATCOM_9.5a`）、`F:`→`libs/`。
  autoexec 設 `WATCOM=D:\`、`PATH=Z:\;D:\BIN;D:\BINB`、`INCLUDE=D:\H`、`WCC386=<旗標>`，再跑 `E:\build.bat`。
  host 端 `subprocess.Popen(["dosbox-x","-silent","-conf",conf])`，輪詢 `build.done` 或 process 退出（預設 timeout 300s）。
- **編譯旗標**（`build_fd2.py:75`）：
  ```
  -bt=dos4g -fp5 -fpi87 -3s -ms -zp4 -i=include -i=F:\ailv3
  ```
  **刻意不加**：`-Dmain`（保留真 `main` 入口）、`-s`（保留原版 stack probe）、任何 `-o*` 最佳化旗標、
  `-DFD2_ASM_PRIMITIVES`（預設走可攜 C 分支）（來源：`rebuild_info/link/wlink_settings.md:44-86`）。
- **旗標經 `WCC386` 環境變數傳入而非寫進 batch 行**——因為 COMMAND.COM 命令列超過約 176 字元會**靜默截斷**，
  曾把 `-fo=<obj>` 的 obj 名截毀（來源：`rebuild_info/build_test/toolchain_quirks.md:24-31`）。
- **連結 directive**（`fd2.lnk`，來源：`rebuild_info/link/wlink_settings.md:14-23`）：
  ```
  system dos4g
  name FD2.EXE
  file lifemain.obj        # 含 main 的 obj 擺第一（決定模組內部名）
  file <其餘 src .obj>
  library ailv3.lib
  library CLIB3S.LIB
  library MATH387S.LIB
  library EMU387.LIB
  ```
  **`system dos4g` 不會自動連 C runtime**，三個 CRT lib 必須顯式列出，否則全部 undefined。
- **輸出**：`workspace/fd2_build/exe/out/FD2.EXE`（+ `obj/`、`build.out`、`--map` 時的 `fd2.map`）。
- **退出碼 0 ⟺ EXE 產出且 0 undefined**；有 undefined 時 `analyze_undefined.py` 分類成 worklist。
- **確定性**：固定 `cputype=pentium_mmx cycles=max`、產生檔明寫 `latin-1` 編碼與換行慣例、每次跑前清乾淨舊 artifact。

### `tools/src_refine/eqcheck.py` 詳解

**要解決的問題**：symbol rename 會讓 binary 產生兩種「binary 可見但行為中性」的擾動，
byte-identical 比對會誤判：

1. **fixup 重排**——wlink 依 symbol 名排序 emit LE Fixup Record Table，改名讓該表 byte 重排（multiset 不變）。
2. **COMDEF 重定位**——未初始化全域是 Watcom COMDEF tentative，wlink 依名排序擺放；
   改一個名可能讓它與鄰居位移幾 byte，連帶改變每個 fixup site 的值與對應 record 的 target 欄。

（來源：`rebuild_info/verification.md:6-21`）

**比對機制**（實作在 `eqcheck.py`）：

- 從 MZ `e_lfanew` 找 LE header，即時 parse `fixup_page_tbl`(+0x68) / `fixup_rec_tbl`(+0x6C) /
  `import_mod_tbl`(+0x70) / `data_off`(+0x80) 等欄位——**完全不硬編位址**。
- `fixup_sites()` 逐 page 走 Fixup Page Table，再逐筆解 Fixup Record 的 src/trg type byte
  （source-list flag `0x20`、target type 0–3、additive flag `0x04`、寬度 bit `0x40`/`0x10`/`0x80`），
  才能正確跳過變長欄位；`SRC_SIZE = {0x00:1, 0x02:0, 0x05:2, 0x06:4, 0x07:4, 0x08:4, 0x09:6}`。
- 兩級判定，任一過即 PASS：
  - **STRICT** — Fixup Record Table 以外 byte-identical，且該表 byte multiset 相同（只發生擾動 1）。
  - **RELOC** — 把 candidate 與 baseline 的每個 fixup site 值與整張 Fixup Record Table 抹零後比殘差 SHA-256；
    殘差相同代表差異只有重定位值 + fixup 重排（擾動 1+2）。
- **baseline**：`tools/src_refine/data/baseline_eq.json`，由 `--gen <baseline_exe>` 產生，
  存 `{size, fixup_lo, fixup_hi, outside_sha256, fixup_multiset_sha256, fixup_len, residual_sha256}`。
- **CLI**：
  ```bash
  python tools/src_refine/eqcheck.py --gen <baseline_exe>   # 建/更新 baseline
  python tools/src_refine/eqcheck.py [built_exe]            # gate；預設吃 build_fd2.py 的產出
  ```
- **失敗訊息**：size 不同 → 「不是 rename-only 改動」；殘差不同 → 「REAL CHANGE：某個落在所有 fixup site 之外的
  code/data byte 不同，無法用重定位/重排解釋」。

**⚠ RELOC 層的盲點（必知）**：RELOC 比對前會把 fixup site 與整張 fixup table 塗白，
所以「**換掉某個 fixup 指向的符號**」對它完全隱形，會被判成 `PASS[RELOC]` 但語意已改
（來源：`tools/src_refine/_index.md:19-24`、`rebuild_info/equivalence/rules.md:194-196`）。
這正是 H 類 bug（教會復活扣款）能溜過去的原因。這類修正要用 **WDISASM 反組譯 `.obj`、直接讀出 fixup 的符號名**驗證。

**姊妹 gate `hash_check.py`**：純 SHA-256 比對 `data/baseline_hash.txt`，
用於 Stage 1（只改註解，必須 byte-identical）。

**`CLAUDE.md:9` 訂的鐵則**：改到 `src/` 時要先判斷會不會改變編出的 exe——
會改變就更新 baseline，不會改變就用 eqcheck 確認新舊 exe 相同。
實際的 baseline 推進在 git 裡是明確的 commit（例：commit `c6fcda29`、`52444dab`：
`src_refine: advance eqcheck/hash baseline to current HEAD build`）。

---

## 4. 還原與驗證 pipeline

### 4.1 從 Ghidra 到可編譯 C 的具體步驟

```
Ghidra decompile
  → 判定 calling convention（per-function disasm，不能假設全域統一）
  → 依四 pool 決定 emit_action：
        fd2            → emit C source
        crt（byte 命中 lookup） → link_vendor_lib（不寫 source）
        crt_equivalent_*        → emit 等價 C function
        ail            → link_vendor_lib（ailv3.lib）
        binary_artifact→ 不 emit（重編會自動重生）
  → 處理 fall-through 六模式（A 共用 epilogue / B 多入口共用 body / C header-only entry /
     D dead fall-through / E data table fragment / F state-machine init-entry）
  → 套用字串與位址鐵則（E-2 不跨 obj dedup / E-3 sub-string anchor / E-3b 絕對位址一律改 symbol）
  → 套用資料 layout 鐵則（相鄰性不變式 / boundary-merge / 跨符號讀取不變式）
  → 寫 unit test（讀檔 function 必須讀真遊戲檔）
  → build gate（compile+link 0 error 0 warning）
  → reviewer 獨立復驗
  → per-function commit
```

（來源：`rebuild_info/equivalence/rules.md`、`pool_classification.md`、`tools/code_emit/_index.md`）

**CRT 的三分法（`rebuild_info/crt/symbol_inventory.md:9-30`）**——這是 emit 路由的關鍵，值得單獨記住：

1. **byte-match 到 vendor lib obj 的** → `link_vendor_lib`，保留 Watcom 原名，**不寫 source**。
2. **`crt_equivalent_*`** → 行為等價於 CRT 但 byte 不 match 任一 lib obj 版本，
   **wlink 無法從 lib 解析，必須手寫等價 C**（FD2 全放 `src/crt/crt.c`）。
   ⚠ 這一類最危險：「看起來像 CRT 就連 lib」的天真策略會靜默產出錯的 binary。
3. **`fd2_*` 的 CRT-style primitive** → 工程師自寫的 CRT 風格 glue（DPMI / global accessor），也要手寫。

另外兩個 Ghidra 造成的陷阱：vendor lib body 會被 Ghidra 過度切分成假的獨立 function
（FD2 案例：`__int7` 內的 fptan worker），**不能 emit 成 C**；
以及靜態連結重複——同一個 CRT helper 因為多個 Watcom RTL obj 各帶一份而出現在兩個位址，
Ghidra 視為兩個同名 function，不需 dedup（`symbol_inventory.md:127-152`）。

### 4.2 「100% 復刻」實際上是什麼

三層等價（`rebuild_info/equivalence/rules.md:9-43`）：

- **Layer 1 — specification-exact（最低保證，全範圍）**：re-link 的 EXE 在 DOSBox-X 內外顯行為與原版完全一致：
  30 章流程、存檔 byte-level 雙向相容、BGM/SFX 觸發時機、同一輸入序列下每 frame 的 mode13h buffer 內容相同。
- **Layer 2 — functionally-exact（所有以 C 重建的 function）**：相同輸入狀態下產出相同 return / register / memory 寫入。
  **不要求 instruction 級 byte 相同**。
- **Layer 3 — byte-exact：不追求。** register allocation、function 排列順序、alignment padding 由 linker/compiler 決定，
  byte-exact 既不切實際也無必要。

**Layer 2 的三個例外（必須對齊原版 codegen）**（`rules.md:45-90`）：

1. **時序敏感的純 CPU 熱迴圈**——功能等價但更短的 codegen 會改變過場時長，破壞與固定時長 SFX 的隱性時序平衡。
   （實證：`>>7` 2 指令 vs `/128` 6 指令，過場從 0.93s 縮到 0.80s，短於 0.9s 的腳步聲 SFX。）
2. **math intrinsic 呼叫形式**——必須在 `#include <math.h>` 前 `#define __NO_MATH_OPS`，
   讓 sqrt/sin/cos 走 named CRT 真函數而非 `__@DSQRT` intrinsic（後者在原 binary 零 xref、從未執行）。
3. **手寫組語函數**——辨識特徵＝無 `PUSH n / CALL __CHK` prologue，且用字串指令 / 硬體 ROL 慣用法
   （wcc386 9.5a 從任何可攜 C 都產不出 LODSB/STOSB/LOOP 形式）。這些一律以可攜 C emit 維持 Layer 2，
   另在 `#ifdef FD2_ASM_PRIMITIVES` 下保留 byte-for-byte 的 `#pragma aux` 分支僅供 A/B 診斷。

### 4.3 四種驗證手段（`rebuild_info/verification.md`）

| 手段 | 層級 | 用途 | 判定 |
|---|---|---|---|
| `hash_check.py` | binary | 一般 `src/` 改動 | 重建 EXE 的 sha256 == `baseline_hash.txt` |
| `eqcheck.py` | binary | symbol rename（byte-identical 會誤判） | STRICT 或 RELOC 任一過 |
| `FD2_REPLAY` build | 載體 | 讓生產碼可被決定論驅動 + 檢查點擷取 | 生產 EXE 與未加 harness 的建置 byte-identical（已 hash 驗證） |
| playthrough golden | runtime | 整合回歸主力 | framebuffer 走 Hamming distance、state 走 byte-equality，比 `tests/play/golden/`；帶 `oracle` 的 scenario 再用同一 LFSR + 傷害公式斷言 |
| 原版 runtime 差分 | runtime | 把 golden 的期望值背書到「原版正解」 | 以 autotype 驅動原版 `~FD2.EXE` 跑到同檢查點，取 DOSBox-X 記憶體 save-state，**用 DGROUP 開頭的 CONST 字串簽章即時定位本次物理基底**（絕不硬編），讀出與 `capture.c` 相同的 16-int32 layout 逐欄比對 |

**互補關係**（`verification.md:75-81`）：hash_check/eqcheck 在 binary 層把關；
FD2_REPLAY 保證被測 codegen == 出貨版；playthrough golden 是主力回歸網；
原版差分才能抓出「golden 本身就抄錯」。

### 4.4 決定論的關鍵技術

- 虛擬時鐘：replay 下 `BIOS_TICK_*` 改成每讀遞增的計數器。
- 輸入注入：`replay.c` 把 scancode 注入 BIOS 鍵盤環，驅動既有輸入路徑（不繞過）；
  選單/對話確認鍵要送 ASCII 而非 raw scancode。
- 檢查點擷取：`capture.c` dump framebuffer（`FBnn`）+ DAC 調色盤（`PALnn`）+ 關鍵全域（`STnn`：16 個 int32 header + 每單位 0x50 byte runtime_char）。
- 所有 hook 包在 `#ifdef FD2_REPLAY` 內，生產版完全不編入。
- 每次跑前重置 `FD2.TMP` / `AUDDBG.TXT` / `FD2.SAV`。

### 4.5 結束偵測（無固定等待）

三個訊號擇一（`rebuild_info/build_test/workflow.md:82-86`）：
`DONE.TXT` 出現／DOSBox process 退出（涵蓋會交回 batch 的 crash）／heartbeat 停滯（`HB.TXT` 每個 test 重寫）。
**heartbeat 必須 fopen/fprintf/fclose**——close 才讓 DOSBox 把重導向的 stdout commit 到 host，`fflush` 不夠。
另外 `gen_run_conf()` 會注入 `[log] logfile=`，run 後掃 protected-mode fault，
用來揭露「hang 其實是 fault」（`-silent` 不會抑制 `[log]` 檔）。

---

## 5. 踩過的坑與工作規範

### 5.1 八類實機 bug（`rebuild_info/build_test/playtest_bugs.md`）

這八類的共同點：**編譯全綠、單元測試全綠都驗不出來**，因為它們不是「function 算錯」，
而是「function 在真實環境的某個隱性契約上和原版不一致」。

| 類 | 隱性契約 | 教訓 |
|---|---|---|
| **A** 跨 vendor 呼叫的暫存器 clobber | vendor function 真正破壞哪些 caller-saved register | vendor lib 的 clobber 行為是 ABI 契約的一部分；`-3s` 的「精確集合」語意下一個都不能漏（只列部分反而把污染搬到沒列的暫存器）。修法：每個 AIL 宣告加 `#pragma aux AIL_<fn> "*" modify [eax ebx ecx edx];` |
| **B** BSS/COMDEF 相鄰與順序 | 未初始化全域的擺放順序與相鄰關係 | **Watcom 對 tentative 定義的順序與相鄰不保證（實測甚至反序）**。凡 reader 把多個相鄰全域當 array 索引或做 struct punning，就必須 emit 成單一真 array / struct，並用 `build_fd2.py --map` 驗證實際 layout |
| **C** 資料型別號性 | 全域的 signed/unsigned | 號性是行為的一部分，不能只看「位元表示一不一樣」；只要進入比較就會在負值/跨零分歧。用 WDISASM 比對 JGE/JLE vs JAE/JB |
| **D** 熱迴圈 codegen 時序 | 純 CPU 熱迴圈的指令數 | 時序敏感熱迴圈不能為了「等價且更短」而簡化。SFX 時長是 real-time（AIL DMA、不隨 DOSBox cycles），繪圖時長隨 cycles 縮放，高 cycles 下特別脆弱 |
| **E** 硬編絕對位址 | 字串/資料的實際位址 | Layer 2 不追求 byte-exact，linker 自由擺放，**任何寫死的絕對位址都會指錯**；一律改 symbol 引用 |
| **F** math intrinsic 呼叫形式 | helper 是 intrinsic 還是真函數 | 「同版編譯器 + 同旗標」不保證呼叫形式對齊——**header 的 intrinsic pragma 也是 codegen 的一部分**。偏離會把原版從未執行過的 vendor 程式碼帶進 runtime，任何 emulator 對那段碼的缺陷都只咬重建版 |
| **G** stack-probe 分佈 | 哪些編譯單元帶 `__CHK` | 這是 load-bearing 的隱性契約，兩邊都不能動：全關失去溢位防護，全開會被中斷的私有堆疊誤殺。用 `#pragma off (check_stack)` 對齊原版分佈，不要用全域 `-s` |
| **H** 折疊基底歸錯符號 | 位址綁到哪個 symbol | 編譯器把常數索引折進位址位移後，Ghidra 會把基底歸給**前一個**符號。判準：**還原出的索引最大值若超出宣告元素數，基底就是被歸錯了**。**eqcheck 驗不出這類**，要用 WDISASM 讀 fixup 的符號名 |

**B / E / H 三類的共同排障啟發**：症狀是「數值或指標讀到不相干的東西」時，
先問這個讀取在原版是不是靠 image layout 才成立的（`playtest_bugs.md:256-258`）。

**定位這類 bug 的主力手段**：host 端 WDISASM 反組譯比對——
直接在 Windows 跑 `WATCOM_9.5a\BINNT\WDISASM.EXE` 反組譯 `.obj`，對照 Ghidra 的原版反組譯逐指令比。
免 DOSBox。注意：**先把 `.obj` 複製到不含 `-` 的暫存目錄**，因為 repo 路徑含 `-`（`fd2-anatomy`）會被 WDISASM 當 option 解析
（`rebuild_info/build_test/workflow.md:51-62`）。
**這條對 `fdps-anatomy` 同樣成立**（路徑也含 `-`）。

### 5.2 Watcom / DOS / DOSBox toolchain 陷阱（`toolchain_quirks.md`）

- **嚴格 C89**：block 內 declaration 必須在第一個 statement 前，混用會連鎖 `E1077: Missing '}'`。
- **無 LFN**：`wcc386` cmdline 可吃長檔名，但 `wlink` 的 `.lnk` directive 與 `wlib` 的 `.rsp` 不行；
  所有 `.c`/`.h` basename 一律 ≤ 8.3。
- **COMMAND.COM batch**：不支援 multi-line `if/else` block（要用 `goto :label`）；
  命令列上限約 176 字元，超過**靜默截斷**。
- **`rename` 不允許 dst 已存在**（DOSBox-X 的 rename 會 silent fail），每次 rename 前 `if exist new del new`。
- **DOS/4G 啟動**：`wlink system dos4g` 產 LE binary，執行需 `DOS4GW.EXE` 在 PATH 或 cwd。
- **Ghidra label 重複**：vendor binary 可能有不同 address 共用同名 label，
  偵測條件是 wlink 的 `redefinition of <symbol> ignored` warning。

### 5.3 Calling convention（`rebuild_info/equivalence/watcom_abi.md`）

- **FD2.LE 是混用的**：自寫 game logic 多數 cdecl 風格；soft-FP / 部分 helper 是 watcall；
  標準 CRT 入口是 cdecl。**cc 必須 per-function disasm 判定，不能假設全域統一**（`watcom_abi.md:9-18`）。
- 判斷信號強度排序：caller 一致 `ADD ESP,K`（最強，→ cdecl）> callee 末指令 `RET N` > caller pre-CALL 設 EAX/EDX/EBX/ECX
  > callee prologue 在 stack-probe 後立刻讀那四個 register > **EBX 在 entry 被當輸入讀且沒先 PUSH**（強烈 watcall 信號）。
- **辨識 prologue 時要跳過 `PUSH n / CALL __CHK` 兩條 stack probe 指令**——它們不是 cc 訊號。
  `__CHK` 刻意 preserve EAX/EDX/ECX/EBX 四個，讓 watcall register 引數安然通過 entry probe。
- 含 varargs 的函式必須 cdecl。
- 宣告 param 數少於實際 ABI → callee 從 stack 讀垃圾、crash。

### 5.4 LE / overlay / segment 結構（`rebuild_info/link/le_layout.md`）

FD2.LE 是 3-object 的 Linear Executable，**沒有 overlay**：

| obj | base | size | 內容 |
|---|---|---|---|
| 1 | `0x10000` | `0x3EBD9` | `_TEXT`（CGROUP），全部 code |
| 2 | `0x50000` | `0x56B0` | DGROUP = `_DATA` + `CONST` + `_BSS` + STACK（4K stack） |
| 3 | `0x60000` | `0x34D2` | FAR_DATA / 非 DGROUP 的大型 data table |

**極重要**：**不能用位址範圍判斷 function 屬於哪一類**——object 1 內 fd2 / crt / ail 三類 function 互相交錯擺放，
沒有清楚的 library/遊戲分區。唯一可靠依據是命名前綴，輔以 caller/callee 與 byte 內容
（`le_layout.md:46-55`、`pool_classification.md:7-11`）。

其他 LE 事實：`resident_name` 是主 `.obj` 的 basename（FD2 是 `"f2"`，與檔名不同）；
`debug_info=0`（連結時未開 debug 或事後 wstrip）；object 2 與 3 之間有 0xA950 byte unused 虛擬位址空間
（wlink 對每個 group 對齊 64K）。

### 5.5 Watcom Easy OMF-386 格式陷阱（`rebuild_info/link/omf_386.md`）

**這份文件零 FD2 專屬內容，FDPS 可以近乎逐字複製。** 只要要寫任何 `.obj` / lib 的解析或 byte-match 工具就會踩到：

- 16-bit / 32-bit record type 成對出現，32-bit = 16-bit type ID 把最低 bit 設起來：
  SEGDEF `0x98`/`0x99`、PUBDEF `0x90`/`0x91`、LEDATA `0xA0`/`0xA1`、FIXUPP `0x9C`/`0x9D`、MODEND `0x8A`/`0x8B`。
- **Quirk 1**：Watcom 有時把 32-bit 內容寫在名義上是 16-bit 的 SEGDEF type `0x98` 下而不換成 `0x99`。
  偵測法：檢查 `body[3]`（應是 NameIdx，必 ≥ 1），若為 0 則 SegLen 其實是 4 byte 寬而非 2。
- **Quirk 2**：ACBP 的 P-bit（本應標示 USE32 segment）**不可信**，要改從 record type `0x99` 或整個檔的 quirkiness 推斷。
- **Quirk 3**：FIXUPP 的 LOCAT 欄是 10-bit big-endian，打包成 `((byte0 & 0x3) << 8) | byte1`。
- **Quirk 4（byte-match 工具的頭號地雷）**：在 USE32 segment 內，`location_type` 1/5
  （名義上是 16-bit offset fixup）**實際是 4-byte fixup**，type ID 仍留在「16-bit」。
  遮罩寬度沒放寬就會靜默漏掉高 2 byte，導致比對誤判。
- **`wlib` 9.5a 的 EXTDEF / PUBDEF32 record buffer 上限約 1KB**，自製 `.obj` writer 要自動分割大 payload
  （`rebuild_info/ail/build_quirks.md:34-37`）。

### 5.6 vendor lib 抽取的教訓（`rebuild_info/ail/`）

FD2 的 Miles AIL 是 **static lib**，沒辦法「呼叫回原 binary」，只能整個從 binary 抽出來重建成可連結的 `.lib`。
若 FDPS 也是同樣情形，以下都適用：

- **策略先決定**：FD2 選的是「重建整套 vendor SDK」——所有 `AIL_*` 都收進去，
  **不做 dead-function 剪枝**，即使遊戲從沒呼叫過（`rebuild_info/ail/inventory.md:16-26`）。
  另一條路是「只重建遊戲呼叫的閉包」，較省但漏掉任何 transitively 需要的內部 helper 就會炸。
- **幸運的識別技巧**：AIL 每個 public entry 都有一個受 `AIL_DEBUG` 環境變數控制的
  `fprintf(log, "AIL_<name>(args)\n", ...)` 自我識別字串——直接從 rodata 的 format string 就能精確命名每個 entry
  （`inventory.md:48-66`）。**FDPS 一開始就該檢查它的 vendor lib 有沒有類似的內嵌 debug print。**
- **手寫 `.obj` 的六條規約（`rebuild_info/ail/omf_emit_rules.md`，全通用）**：
  R-1 segment 名必須用 Watcom 預設（`_TEXT`/`_DATA`），自訂名會讓 DS-implicit 存取壞掉；
  R-2 BSS 要 emit 成 file-backed 全零 `_DATA`（完整 `LEDATA32`），獨立的 BSS-class SEGDEF 會在 DOS4GW 下觸發無界 page-commit hang；
  R-3 **每個 FIXUPP32 site 的 LEDATA byte 必須先歸零**，因為 wlink 是 addend 語意
  （`final = addend + target [- src - 4]`），留著原本的位移會變成兩倍偏移、跳飛；
  R-4/R-5 mid-function / end-exclusive sentinel 要補合成 label 的 PUBDEF；
  R-6 **所有** function entry（不只 public，連 internal 都要）都要有 PUBDEF，
  漏一個會在 runtime 靜默 wild jump、linker 完全不警告。
- **`#pragma aux` 無法表達的 tail-JMP**：兩個 thunk 是 tail-JMP 到別的符號，相對位移無法用純 `#pragma aux` 編碼；
  解法是寫一個 inline `#pragma aux` helper（只能呼叫、不能取位址）加一個獨立的 out-of-line PUBDEF wrapper
  （`crt/symbol_inventory.md:75-80`）。
- **AIL public API 的語意陷阱**（若 FDPS 也用同版 AIL 可直接沿用）：
  `AIL_delay(N)` 數的是 **VGA vertical retrace 次數**（約 16.67ms/次 @60Hz），不是毫秒；
  `AIL_sequence_status` 的 bitflag **不連號**（1=FREE、2=DONE、**4**=PLAYING、8=STOPPED）；
  `AIL_set_sequence_loop_count(h, 0)` 是**無限循環**而非零次（`rebuild_info/ail/public_api_semantics.md:47-68`）。
- **AIL 的內部 allocator slot 必須在 `AIL_startup` 前 patch 成 CRT `malloc`/`free`**
  （`rebuild_info/ail/calling_convention.md:83-96`）。
- **A 類 clobber bug 的完整機制**（`ail/calling_convention.md:22-52`）：
  Watcom 的 `#pragma aux ... modify [...]` 清單是**精確集合**語意。
  在 `-3r`（register cc）下 EAX/EBX/ECX/EDX 本來就是 volatile，寫不全也沒事；
  但在 **`-3s`（FD2 遊戲碼用的）** 下 EBX 預設 callee-saved，
  **只寫 `modify [ebx]` 會讓編譯器誤以為 EAX/ECX/EDX 被保存，只是把污染搬家而不是修好**。
  **所以四個 caller-saved register 一定要全列**：
  ```c
  extern <ret> __cdecl AIL_<fn>(<args>);
  #pragma aux AIL_<fn> "*" modify [eax ebx ecx edx];
  ```
  **FDPS 若在 `-3s` 下連任何 Watcom vendor static lib，建議一開始就套這個 pattern，
  不要等靜音 bug 冒出來才重新發現。**
- **BLASTER 環境變數的 IRQ 必須與模擬器設定的 SB IRQ 一致**，否則 SFX 靜默失敗、無任何錯誤訊息
  （`rebuild_info/ail/build_quirks.md:7-19`）。

### 5.7 資源檔格式解析

- FD2 的 11 個資源檔共用一個**「LLLLLL」archive 格式**：
  `+0x00` 6 bytes signature `b"LLLLLL"`，`+0x06` u32 LE × N 的 offset 表（最後一筆是 sentinel = file_size），
  之後是串接的 payload。entry_count = N − 1（`resource_info/overview.md:7-21`）。
  **這是漢堂自家格式**，KB 明述「與 LLLLLL DAT 命名同源」「同一 build pipeline」（`resource_info/fdicon.md:63`、`title.md:20`）。
- decoder 的正確性靠**與 `src/` 的真實解碼函數對齊**驗證：
  例如 commit `01bc73e8`「fix RLE opcode semantics to match `fd2_rle_blit_sprite`」——
  **先寫的 decoder 是錯的，是靠重建出來的 C 反過來修正的**。這是「先做程式重建、再回頭校正資源解碼器」的順序證據。
- `tools/rsrc_unresolved/verify_dead.py` 的做法值得抄：**窮舉 `src/` 內所有載入點**來判定哪些資源 index 是 dead，
  而不是靠猜。FD2 一度誤判 12 個 dead，實際只有 3 個（commit `79639094`）。

### 5.8 工作規範（`CLAUDE.md`，建議 FDPS 沿用）

- 每個新 session 開始要讀 `README.md` 了解 KB 結構。
- 開始規劃或執行任何 plan 之前，**先確認可用工具都能用，否則立刻停下**（Ghidra MCP / DOSBox-X / Watcom）。
- **所有 deferred / backlog 項目在工作結束前都要被深入研究和解決**；真的無法處理才問使用者，
  使用者確認無法當下解決，才寫進 `open_issues.md`。
- Script 放 `tools/{工作名稱}/`，中間產物與輸出放 `workspace/{工作名稱}/`。
- Ghidra 操作規範：改動後檢查 error bookmark（`list_bookmarks(category="Bad Instruction")`）並全部修復；
  **error bookmark 修正後不會自己消失，要手動移除**；檢查變更 function 的 calling convention；工作完成後儲存變更。
- DOSBox-X 一律用 `-silent` 以達成全自動化。
- emitter / build gate **一律前景跑，嚴禁 `run_in_background`**——subagent 一交出最終訊息就結束、
  收不到背景通知、不閉環，且會留下 dosbox 孤兒程序（`tools/code_emit/_index.md:43`）。
- 每批 ≤ 12 個 function（checkpoint 粒度），跑完 hard-stop 等使用者確認（`tools/code_emit/_index.md:47`）。
- 讀檔 function 的測試**必須讀 staged 的真遊戲檔**並對真實解析值斷言；
  禁止捏造假檔、禁止 `remove()` staged 真檔（`rebuild_info/build_test/workflow.md:88-94`）。
- 命名的位址殘留 vs 領域 ID 靠**語意人工判定，嚴禁 regex 機械剝除**（`tools/src_refine/_index.md:51`）。

### 5.9 流程層面的教訓

- **並行 emit 會產生「協調落地」債**：`open_issues.md:24-35` 的 #32/#33 兩個 issue 都是
  「spy/stub 住在共用的 `tests/testglob.c`、被數百處跨分支引用，導致 19 個 blit-leaf 與 2 個 pathfind entry
  無法逐一落地，必須等所有分支合併後當一個 coordinated unit 落地」。
  → **FDPS 若要並行，切分區時要把「共用 stub 的叢集」整包放在同一分區**。
- **spy 單元測試在真 body 落地後會變成負債**（W1027 redefinition），FD2 最後整批退役到 `legacy/`（commit `41e8d64e`）。
  → 一開始就規劃好 spy 的退場路徑。
- **KB 會漂移**：`kb-overhaul` batch7 的逐檔機械驗證在五個資料夾各 patch 出 5~43 處 factual drift。
  → 大量產出後要排一輪「拿 ground truth 逐條驗證 KB」的工作，不要相信寫過就是對的。
- **cp950 編碼災難**：commit `d0ae5677` 修了 `emit_issues.json` 的 cp950 亂碼。
  → Windows + 繁中環境下，所有 JSON / md 的讀寫都要顯式 `encoding="utf-8"`（實測 `routing.json` 不指定 encoding 就炸）。

---

## 6. 可直接沿用 vs 必須重做

### 6.1 可直接複製／沿用（幾乎零改動）

| 項目 | 說明 |
|---|---|
| **KB 資料夾架構與 `_index.md` 規範** | `program_info/` + `resource_info/` + `assets/` + `chapters/` + `rebuild_info/` + `tools/` + `workspace/` + `legacy/` 的四視角切分與單一擁有者原則。FDPS 的 chapters 若不是 30 章制，改成對應的關卡單位即可 |
| **`CLAUDE.md` 的工作規範段** | §5.8 全部條目，只需把 FD2 換成 FDPS |
| **`.gitignore`** | 一字不改可用（把 `fd2_game_files/` 換成 `fdps_game_files/`，實測 FDPS 已這麼做） |
| **命名慣例** | `fdps_` / `data_fdps_` 前綴 + `crt_`/`AIL_` 豁免 + 四 pool 分類；8.3 檔名限制 |
| **`tools/src_refine/eqcheck.py`** | LE fixup parser 完全通用（純從 header 解析、不硬編），只需改預設路徑常數 |
| **`tools/src_refine/hash_check.py`** | 純 SHA-256 gate，通用 |
| **`tools/ail_extract/omf_writer.py`** | 通用 32-bit OMF record encoder |
| **`tools/oob_index_audit/scan_oob_index.py`** | C 陣列宣告/下標解析與折疊基底分類邏輯通用，只需改 `data_fd2_` 前綴過濾 |
| **`tools/fd2_play/fb2png.py`** | 自帶 zlib 的 PNG encoder，零依賴，通用 |
| **`tools/publish/publish_public.py`** | 發佈流程通用（改 remote 與 PII 清單） |
| **`build_fd2.py` / `build_test.py` / `build_replay.py` 的架構** | DOSBox-X 掛載 + 三訊號結束偵測（DONE.TXT / process 退出 / heartbeat 停滯）+ 前景輪詢，這套 pattern 可直接照抄 |
| **`.claude/skills/ghidra-usage`** | binary-agnostic，FDPS 已有一份；建議把 FD2 版多出的 `CROSS_VERSION_FUNCTION_MATCHING.md` / `CROSS_VERSION_MATCHING_COMPREHENSIVE.md` / `BINARY_DOCUMENTATION_ORDER.md` 補進來 |
| **§5.1 八類 bug 的教訓表** | 這是本 playbook 最有價值的部分，可直接當 FDPS 的 emit checklist |
| **§5.2 toolchain 陷阱** | 只要 FDPS 也用 Watcom + DOSBox-X 就完全適用 |
| **§5.3 calling convention 判定規則** | Watcom 32-bit ABI 通用 |
| **`rebuild_info/link/omf_386.md`** | **零 FD2 專屬內容，可逐字複製**（Easy OMF-386 四大 quirk） |
| **`rebuild_info/build_test/toolchain_quirks.md`** | **零 FD2 專屬內容，可逐字複製** |
| **`rebuild_info/equivalence/watcom_abi.md`** | ABI 推導方法論（cc 判定規則、`__CHK` 機制、param 數推導）通用，只有舉例的位址是 FD2 的 |
| **`rebuild_info/equivalence/rules.md` 的六種 fall-through 模式** | 通用分類法 |
| **`rebuild_info/ail/omf_emit_rules.md`（R-1~R-6）** | 若 FDPS 也需要從 binary bytes 合成 `.obj` 就完全適用 |
| **`rebuild_info/ail/calling_convention.md` 的 clobber pragma pattern** | 只要在 `-3s` 下連 Watcom vendor static lib 就適用（見 §5.6） |
| **`rebuild_info/verification.md` 的四手段架構** | 尤其 eqcheck 兩級設計與「用 CONST 字串簽章在 save-state 裡定位 DGROUP」的原版差分技巧 |
| **`rebuild_info/crt/fid_match.md` 的版本判定方法論** | 建多版 fidb + 對一小組「版本判別函式」跨版 hash + 取交集，流程通用（指紋函式本身要重找） |
| **`rebuild_info/crt/symbol_inventory.md` 的三分法** | link_vendor / `crt_equivalent_*`（必須手寫）/ 專案 glue，通用分類 |
| **`decoders/dat_header_parser.py`** | **若 FDPS 的 `.VFS` 也用 LLLLLL 格式就可直接用**；需先驗證（見 §6.2） |

### 6.2 必須重新求證（FD2 專屬）

| 項目 | 為什麼 |
|---|---|
| **所有位址** | `0x51B91`、`0x5266B`、`0x6238D`、`0x627b8` 等全部是 FD2.LE 的 link-time vaddr |
| **所有 struct layout** | `runtime_char` 80B、item 23B、spell 7B、character base 24B、growth 11B、enemy 10B 等 stride 全是 FD2 的 |
| **資源檔容器格式** | **FD2 是 11 個 `LLLLLL` DAT + FDICON.B24；FDPS 是 8 個 `.VFS` + 2 個 `.CEL`——容器名稱與副檔名都不同，格式必須重新解析。** 但兩者都是漢堂自家 build pipeline 的產物（`resource_info/title.md:20`），值得先拿 LLLLLL 的 offset-table 假設去試 |
| **編譯器與 CRT 版本** | 必須用同一套方法重新判定（見下方觀察） |
| **AIL 版本與抽取邊界** | `libs/ailv3/ailv3.lib` 是從 **FD2.LE** 抽出來的，函式邊界與 fixup verdict（1408 + 244 筆人工判定）都綁 FD2。FDPS 必須重抽 |
| **`fd2.lnk` 的 object 分布與 stack size** | `option stack=4K` 是從 FD2 binary 反推的 |
| **存檔格式** | FD2.SAV 22987 byte 的 layout 是 FD2 的（但見下方觀察） |
| **章節 / 事件 dispatch 架構** | FD2 的 chapter event 是「直接函數 dispatch，非 bytecode」（`open_issues.md:79`）——FDPS 未必相同 |
| **`switch` 是否編成 jump table** | FD2 的**遊戲端 0 個 indirect JMP**（全 binary 63 個全在 CRT/AIL），`switch` 一律編成 if/else 鏈（`rebuild_info/equivalence/rules.md:250-255`）。這決定「fall-through 模式 E（data table fragment）只出現在 CRT 段」這個省事的結論是否成立，FDPS 必須重驗 |
| **哪些 function 是 `crt_equivalent_*`** | 「byte 不 match 任一 lib obj 但行為等價」的清單完全取決於該 binary 連的 lib 版本，必須重跑 byte-match audit |
| **wcc386 / wlink 旗標組** | FD2 的 `-bt=dos4g -fp5 -fpi87 -3s -ms -zp4` 是從 binary 反推的（記憶體模型、FP 模型、cc 都可能不同） |
| **`chevt_audit` / `kb_overhaul` / `growth_table` / `glyph` / `fd2_diff` 內的所有腳本** | 全部硬編 FD2 位址與表結構 |

### 6.3 系列作品關聯：KB 的記載與實地觀察

**KB 的記載**：`fd2-anatomy` **完全沒有提到 FDPS / 炎龍騎士團外傳或其他系列作品**
（實測 grep `外傳|FDPS|FD1|前作|續作|同系列|一代|三代` 於全 KB，僅命中無關內容）。
唯一的系列線索是「**漢堂**（開發商）自家的 LLLLLL DAT 格式與命名同源、同一 build pipeline」
（`resource_info/fdicon.md:63`、`resource_info/title.md:20`）與 `assets/tables/_index.md:39`
提到「各表沿用漢堂攻略本的兩字母欄碼」。

**以下是我對 `fdps_game_files/` 的直接檔案觀察（不是 fd2-anatomy 的記載），列為需驗證的線索**：

| 觀察 | FD2 | FDPS | 意義 |
|---|---|---|---|
| Miles AIL driver 檔 | `ADRV688.DIG` 11278B、`JAMMER.DIG` 3219B、`PROAUDIO.DIG` 2127B、`RAP10.DIG` 2849B、`AILDRVR.LST` 16131B、`DIG.INI` 207B、`SETSOUND.EXE` 167953B（皆 1995-01-18） | **完全相同的檔名與 byte 大小、相同日期** | AIL vendor 套件很可能同版；`rebuild_info/ail/` 的 ABI 契約（clobber pragma、handle 型別）與 `tools/ail_extract/` pipeline 高機率可沿用 |
| `.MDI` MIDI driver | 有（ADLIB / OPL3 / MT32MPU / …） | **沒有** | FDPS 可能只用 digital audio、無 MIDI BGM；AIL3MDI 那半邊可能不存在 |
| `DOS4GW.EXE` | 244716B（1993-09-01） | 265420B（1994-09-01） | **DOS/4GW 版本不同**，extender 行為/stub 需重新確認 |
| LE 大小 | FD2.LE 346650 / FD2.EXE 357074 → stub = **10424B** | FDPS.LE 362469 / FDPS.EXE 373301 → stub = **10832B** | **Watcom DOS bind stub 大小不同 ⇒ wlink/Watcom 版本很可能不同於 9.5a。這是 FDPS 最該優先驗證的一件事** |
| 存檔 | `FD2.SAV` **22987 byte** | `FDE.SAV` **22987 byte** | **完全相同的大小。** 存檔 layout 極可能高度共用，`resource_info/save_format.md` 值得優先拿來比對（含 XOR involution crypt + checksum，見 `tools/fd2_play/gen_scenario.py`） |
| runtime 暫存檔 | `FD2.TMP`（portrait cache dump） | `MER1/MER2/FMER1/FMER2.TMP`（18432 / 4096 byte） | 都是 runtime swap，機制可能類似但檔案切分不同 |

> ⚠ 上表右半是我對檔案清單/大小的直接觀察，**不是 fd2-anatomy 的記載**，也未做任何內容比對，一律需要實際驗證。

---

## 7. 未記載 / 需向使用者確認的事項

1. **git 之前的工作內容未記載。** 第一個 commit 就有 200 檔 44,631 行，那一輪（資源檔格式初解、攻略資料建庫、
   Ghidra 初次全域命名）花了多久、怎麼做的，repo 內沒有記載。**建議直接問使用者。**
2. **`.claude/skills/fd2-knowledge` 的來源資料未記載。** 該 skill 由 `build_index.py` 從攻略本 HTML 建出，
   但原始 HTML 不在 repo 內。FDPS 若要建同樣的 skill，需要另尋 FDPS 攻略資料。
3. **Ghidra 專案本身不在 repo 內。** 整個 KB 大量以「Ghidra 為 live source of truth」寫作
   （多處明寫「即時查證，不從本表抄」），意味著 FD2 的 Ghidra 專案檔是必要但未版控的資產。
   **FDPS 要確認 Ghidra 專案的保存與備份方式。**
4. **`workflow.js` 執行環境未記載。** `emit_review.wf.js` / `coland.wf.js` / `data_emit.wf.js` / `src_refine.wf.js`
   是用某個 `Workflow({scriptPath, args})` 工具跑的，該工具本身不在 repo 內，其可用性需確認。
5. **`libs/ailv3/ailv3.lib` 能否給 FDPS 用未記載。** 需先確認 FDPS 連的是不是同版 AIL。
6. **`fd2-anatomy` 的 open issues 有 6 項仍未關閉**（`open_issues.md:8-69`），其中與 FDPS 相關的通用問題是
   `#35`（DPMI extender 偵測路徑未經 emulator 實機確認）與 `#31`（CRT byte_match 全表 false-positive 未系統性複驗）。
7. **FDPS 是否有 95/98 雙版本問題未知。** FD2 有 1995 初版與 1998 合輯版，只有 3 個檔案真的不同
   （`resource_info/version_diff.md`）。FDPS 手上的檔案日期是 1997-12 ~ 1998-01，是否存在其他版本需確認。
8. **FDPS 的目標範圍未確認。** FD2 專案做到「完整重建 + 決定論測試 + 公開發佈 + 互動資料網頁」，
   總計 2338 commit。FDPS 是否要走完同樣的深度（尤其 §4 的四套驗證手段與 §6 的 src_refine 全符號精修），
   建議先與使用者對齊，因為這直接決定要不要在 Phase A 就把 KB 骨架與 build gate 一起立起來。
