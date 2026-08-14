這是繁體中文版 DOS 遊戲「炎龍騎士團外傳」(FDPS) 的逆向工程專案。

目標：完全理解 Ghidra 對 `FDPS.LE` 產生的 decompiled source，還原出完整 C 原始碼，並用 Watcom C/C++ 10.0a 重新編譯出在 DOS 環境下**功能等價**的執行檔。

- 詞彙表：@CONTEXT.md
- 決策記錄：`docs/adr/`
- 前作 FD2 專案的完整 playbook：`docs/research/fd2-playbook.md`

## 鐵則

**禁止批次或抽樣決定 function 的身分、calling convention 與 emit。** 每個 function 都必須逐一親自讀過 assembly 才下判斷，一次處理一個。禁止用腳本或規則批次套用，禁止抽樣後外推。詳見 ADR-0002。

這條規則的邊界：對整體事實的調查（編譯器版本判定、容器格式解析、統計比對）仍可用腳本，那不是在替每個 function 下判斷。

平行化必須靠 workflow 腳本驅動，工作清單只存在於腳本中，每次 agent 呼叫只帶一個 function。**不可以把 function 清單交給單一 agent 讓它自行分配**——實測證明 agent 會退化成批次處理。

## 工作步驟

- 每個新 session 開始時先讀 @README.md 了解知識庫結構（README 尚未建立時，讀 `docs/research/fd2-playbook.md`）
- 開始規劃或執行每個 plan 之前，先確認下述可用工具都能使用，否則立刻停下來等使用者檢查
- 工作過程中產生的所有 deferred / backlog 項目，在整個工作結束前都要被深入研究並解決。真的遇到無法處理的狀況才詢問使用者；使用者確認無法當下解決，才寫進 `open_issues.md`

## 語言規範

程式碼與 scripts 的識別字、註解、docstring、輸出訊息，以及 Ghidra 內的 plate comment、decompiler comment、disassembly comment、符號名稱，一律用英文。唯一的例外是遊戲內的專有名詞（角色、章節、物品名稱等），保留原文。

知識庫、devlog、ADR、issue 與對使用者的回覆用繁體中文。

## Scripts 規範

- 新增的 scripts 放在 `tools/{工作名稱}/` 下
- Scripts 的 pipeline intermediate / output 一律寫到 `workspace/{工作名稱}/` 下

## 知識庫寫作規範

知識庫記錄「結論是什麼」，`devlog/` 記錄「怎麼走到這個結論」。兩者嚴格分離。

- 工作過程中得到的確定結論都要整合進知識庫，並確認內容沒有重複
- 新撰寫的內容必須是**最後的結論**，不能是描述分析過程的流水帳
- 不能引用 `legacy/` 或 `workspace/` 底下的東西
- 任何文件與 scripts 的更新都要反映到對應的 `_index.md`
- 修改完文件後要再次 review，確認沒有出現「在某個時間解出、以前原本是什麼、phase」這類流水帳內容，若有立刻修正

## Devlog 規範

- `devlog/YYYY-MM-DD-<主題>.md`，一個工作段落一篇
- 敘事體，明確允許流水帳：試了什麼、失敗了什麼、為什麼放棄某個方向、當時的判斷依據
- **重點記死路**——成功的路徑會沉澱進知識庫與程式碼，失敗的路徑才是唯一會遺失的資訊
- 詳細程度的標準：三個月後回來看，能不能重建當時的判斷
- Workflow 每次跑完的 agent 回報 JSON 歸檔到 `devlog/runs/`，與人寫的敘事分開
- 不需要索引，不需要去重

## 落地節奏

每個工作段落結束要 commit 時，四件事同時落地：程式碼、知識庫、Ghidra 文字快照（`tools/ghidra_snapshot/`）、devlog 一篇。

## 可用的工具

- **Ghidra 12.1.2**：`C:\Users\fdpsf\Documents\ghidra_12.1.2_PUBLIC`，專案在 `C:\Users\fdpsf\Documents\reverse_fdps`，已開啟 `FDPS.LE`，透過 Ghidra MCP 存取。使用前參考 `.claude/skills/ghidra-usage/`
- **DOSBox-X**：`C:\DOSBox-X`，要用 silent mode（`-silent`）執行以達成全自動化
- **Watcom C/C++ 10.0a**：`C:\Users\fdpsf\Documents\WATCOM_10_series\WATCOM_10.0a`（其他版本同目錄下，供比對用），要在 DOSBox-X 裡執行
- **光碟映像**：`D:\Game\Flame Dragon\fdps_image\FDPS_DISC_1.cue`（另有 DISC 2）。DOSBox 掛載指令：`imgmount e -t cdrom "D:\Game\Flame Dragon\fdps_image\FDPS_DISC_1.cue"`
- **FDPS 攻略站**：`https://chiuinan.github.io/game/game/intro/ch/c31/fdps/fdps/`（注意 `fdps` 出現兩次）

## Ghidra 操作規範

- 對 `FDPS.LE` 進行修改後（新建 function、重新 decompile 等），結束時要：
  - 檢查有沒有產生 error bookmark，若有全部修復。搜尋指令是 `list_bookmarks(category="Bad Instruction")`
  - 檢查發生變更的 function 的 calling convention 是否正確，有錯全部修復
- Error bookmark 在問題修正後不會自己消失，要手動移除
- 對 `FDPS.LE` 修改過的話，工作完成後要儲存變更
