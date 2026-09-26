# kb_verify — 全知識庫逐條驗證與跨文件一致性

票 25.17 的兩段 workflow 與它們不需判斷的另一半：知識庫裡人手寫的每一份文件，一份（或長文件的一段）一個 agent 逐條對照 `src/`、Ghidra 與遊戲檔驗證；接著把逐文件驗證結構上看不到的跨文件問題——一處改了另一處沒改的同一個錯、同一個事實有兩個擁有者、驗證者順手看到的矛盾——用確定性的方法找出候選，一組一個 agent 判定。

前作 `fd2-anatomy` 的知識庫翻新（commit `ad85325b`–`2bfa777e` 的 batch 7）也是逐檔逐條驗證、再做跨 `_index.md` 的一致性收尾，但驗證由主 session 逐一親核，沒有留下工具。本工具沿用它的兩段形狀，驗證與落地照 [ADR-0007](../../docs/adr/0007-workflow-automation-and-agent-context.md) 拆開、全自動跑完。

## 範圍

逐條驗證的對象由 `kbverify.py` 的 `VERIFIED_GLOBS` 決定：`CONTEXT.md`、`program_info/`、`resource_info/`、`rebuild_info/`、`assets/` 裡人手寫的文件與 `tables/`、`assets/text/_index.md`、30 份章節頁與 `chapters/_index.md`、`libs/_index.md`、`ghidra_snapshot/_index.md`。

不派 agent、只跑閘門的（`SCOPE_NOTES`）：由產生器寫出、閘門逐格比對資料的內容——`assets/text/` 的三頁（`global_text.py verify`、`glyph_table.py build` 重產無差異）、`tools/data_tables` 產生的表（`data_tables.py check`）、章節頁與 `chapters/_index.md` 夾在標記裡的產生區塊（`check_chapter.py --landed-all`、`index.py verify`、`chapter_facts.py verify-maps`），以及已經逐條驗證過的 `cut_content/`（`cut_content.py check`／`verify-media`、`story.py check --final`）。人手寫但由 `tools/data_skill` 逐列對照紀錄的表（物品、法術、職業、人物的出場屬性與成長）留在驗證單元裡，顯示時標明哪些欄已被閘門涵蓋。

長文件在 `##` 節切成數段，一段最多約 15,000 字（產生區塊不算），一段仍然只給一個 agent；第一次跑時把切法凍結（`units --freeze`），之後落地改變某段長度也不會移動其他段的邊界。

## 檔案

| 檔案 | 用途 |
| --- | --- |
| `kbverify.py` | 逐文件驗證不需判斷的部分：`units`（切段與凍結）、`show`（一段的內容、`unit_sha1` 與確定性檢查在本段的發現，判定 agent 看到它的唯一途徑）、`pending`、`check`（判定檔的 gate：欄位、`unit_sha1`、改錯類的發現要有一手證據、每筆修正的 `old` 在檔案裡恰好出現一次且在本段內、不在產生區塊裡）、`rescan`（有修正、有外部修正、信心不是 high 或判斷不了的發現交第二位 agent）、`report`、`apply`（只落地第二位 agent 確認或修改後的修正，落地後在判定檔記 `_landed_sha1`；`chapters/_index.md` 的前言與跨章機制鏈同步寫回 `tools/chapter_docs/index.py` 與它的草稿）、`lint`（全知識庫的確定性規則：禁引 `workspace/`／`legacy/`、流水帳字眼、相對連結、`名稱`（`位址`）對快照、未知的 `fdps_` 符號，規則本身 import 自 `game_mechanics/check_mechanics.py`）、`indexes`（各資料夾 `_index.md` 列了每個檔、每個 `tools/` 子資料夾有 `_index.md` 且被總表列出、README 描述了每個頂層資料夾） |
| `verify_ticket25_17.js` | 第一段 workflow：一段一個 agent 驗證、每輪 gate、失敗重試一次、第二位 agent 回掃（回掃弄壞 gate 的也重試一次）、落地（`apply` 後跑全部知識庫閘門，閘門不過只回報、不自行修）、收尾報告與判定彙整到 `devlog/runs/<date>-kb-verify-*.json` |
| `kbconsist.py` | 跨文件一致性不需判斷的部分：`freeze` 從五個來源產生並凍結候選項目——第一段落地的每筆事實修正（錨點：位址、`fdps_` 符號、檔名）在其他頁與同頁其他地方出現的行（P）、字元 4-gram 大量重疊的段落（D，只比文字、不比程式碼引用，章節頁之間與 `_index.md` 描述自己資料夾頁面的列不算，連成一串超過 6 段的拆成兩兩一組）、驗證者回報的跨文件矛盾（X，同一對頁面同一錨點合成一項）、驗證者提出的踩雷點候選（PF）、票面要求重判的 `cut_content/` 分類（R，一個條目一項）；`show`、`check`、`rescan`、`report`、`apply`（同樣只落地第二位 agent 確認或修改的修正） |
| `consist_ticket25_17.js` | 第二段 workflow：一項一個 agent、gate、回掃、落地（`apply`、`cut_content.py index` 重產總表、全部閘門）、收尾報告到 `devlog/runs/<date>-kb-consist-*.json`；要在第一段落地之後才跑，擴散項目是從落地紀錄產生的 |
| `kbrefused.py` | 一致性段落地時被拒收的修正（`old` 已被同一段落上先落地的另一筆修正改掉）：`triage` 把新文字已經逐字在頁面上的判為已涵蓋、記進 `workspace/kb_refused/triage.json`，其餘每一筆凍結成一個項目；其他子命令沿用 `kbconsist.py` 的判定檔格式、gate、回掃與落地（`use_workspace` 換成自己的項目清單與判定檔） |
| `refused_ticket25_17.js` | 第三段 workflow：被拒收的修正一筆一個 agent，判斷現行文字是否已經說出那筆修正要說的事實，不是就寫出對現行文字的修正；gate、回掃、落地與全部閘門、收尾報告到 `devlog/runs/<date>-kb-refused-*.json` |
| `kboutside.py` | 前三段判定裡的 `outside`（修正不在被判的那一頁：`src/`／`tests/` 註解、`tools/` 的產生器與手寫資料、別的頁、票、Ghidra）：`freeze` 把三份收尾報告的每一條依它提到的非知識庫檔案以 union-find 分組（只提到知識庫頁的依第一頁），同一段註解的多條請求落在同一組、由同一個 agent 一次寫好；`apply` 只落地第二位 agent 確認或修改的修正，C 原始碼的修正整檔比對（`strip_c` 之後）必須只差註解或只差識別字改名，不寫 `docs/adr/`、`devlog/`、`README.md`，重產只能從固定清單選（`REGENERATE`）；`ghidra` 印出要由票的 session 逐項做的 Ghidra 改動 |
| `outside_ticket25_17.js` | 第四段 workflow：一組一個 agent，逐條判斷請求是否已做、是否屬實，屬實的寫出修正；gate、回掃、落地（`apply`、重產、知識庫閘門、動到的工具的單元測試、完整建置閘門）、收尾報告到 `devlog/runs/<date>-kb-outside-*.json` |
| `test_kb_verify.py` | 單元測試：`python -m unittest tools/kb_verify/test_kb_verify.py` |

判定檔在 `workspace/kb_verify/verdicts/` 與 `workspace/kb_consist/verdicts/`，落地紀錄在 `workspace/kb_verify/applied.json`。

## 執行順序

```
Workflow({ scriptPath: "tools/kb_verify/verify_ticket25_17.js",
           args: { date: "YYYY-MM-DD", exclude: [還不能改的頁] } })
Workflow({ scriptPath: "tools/kb_verify/consist_ticket25_17.js",
           args: { date: "YYYY-MM-DD", exclude: [還不能改的頁] } })
Workflow({ scriptPath: "tools/kb_verify/refused_ticket25_17.js",
           args: { date: "YYYY-MM-DD" } })                     （一致性段有拒收時）
Workflow({ scriptPath: "tools/kb_verify/outside_ticket25_17.js",
           args: { date: "YYYY-MM-DD" } })                     （最後：前三段的 outside）
python tools/kb_verify/kboutside.py ghidra                    （票的 session 逐項做 Ghidra 改動）
```

兩支的最後都有落地段：`apply` 只落地第二位 agent 確認或修改的修正，其餘每一項（沒有判定、判定不過 gate、還沒回掃、`old` 已經不是恰好一次、`exclude` 擋下的頁）都以名字列在拒收清單，不靜默略過；接著跑全部知識庫閘門（`check_chapter.py --landed-all`、`index.py verify`、`data_tables.py check`、`global_text.py verify`、`cut_content.py check`、`story.py check --final`、`data_skill/build.py`、`kbverify.py indexes`），`lint` 的剩餘筆數只列出、由票的 session 人工檢視（驗證者判定「照原樣成立」的會留在裡面）。閘門不過時落地段只回報，修正要判斷，歸票的 session。`exclude` 擋下的頁之後直接再跑一次 `apply` 即可補落地。

需要改 `src/` 註解、Ghidra 或產生器的修正不在前三段落地：判定檔的 `outside` 欄寫明位置與內容，收尾報告彙整成清單，由第四段逐組判定並落地（落地後跑完整建置閘門）；Ghidra 改動由票的 session 照 `kboutside.py ghidra` 逐項做，做完查 bookmark、calling convention、存檔並匯出快照。

兩支都可重跑續跑：判定檔通過 gate 的項目不再判一次，已落地的判定檔帶 `_landed_sha1`／`_landed`，不會落地兩次。

`chapters/_index.md` 由 `tools/chapter_docs/index.py` 從固定前言與跨章機制鏈的草稿組成，所以落地時前言的修正同步寫進 `index.py` 的 `INTRO`、機制鏈的修正同步寫進它的草稿，下一次 `index.py verify` 才會一致；碰到產生表的修正一律拒收。
