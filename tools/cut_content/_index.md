# tools/cut_content — 刪減與未用資料夾的共同入口

[`cut_content/`](../../cut_content/_index.md) 的總表產生、結構閘門與素材重生都從這裡進去。票 25.10 建立骨架，25.11–25.14 各自註冊自己主題的素材產生器。

前作沒有對應成果：`fd2-anatomy` 沒有刪減與未用的資料夾，`fd2-anatomy/tools/rsrc_unresolved` 只是資源死碼的一次性驗證。素材的解碼沿用本專案的擁有者（[`cel_decode`](../cel_decode/_index.md)、[`saf_decode`](../saf_decode/_index.md)、[`vfs_dump`](../vfs_dump/_index.md)），這裡不另寫解碼器。

## 檔案

| 檔案 | 用途 |
| --- | --- |
| `cut_content.py` | 四個子命令（見下）；素材產生器的註冊表 `GENERATORS`；給產生器用的讀檔與寫檔函式 |
| `test_cut_content.py` | 單元測試：`python -m unittest tools/cut_content/test_cut_content.py` |
| `story.py` | `story` 主題（票 25.14）：`story.md` 產生區塊（對白全文、部署表、素材表、永遠不會顯示的文字總表）的 `build` 與閘門 `check`；永遠不會顯示的文字的歸屬表 `OWNERS`——每一條都要歸到一個條目或排除項，章節區塊以 `tools/chapter_docs/judgements/` 的逐章判定為準，`check --final` 不接受待判定；素材產生器（整張地圖、`ICON0032.SAF` 逐格與總覽及內嵌音效、`M09.CEL`／`M090.CEL` 總覽）；`cdda` 從光碟映像切出 CD 音軌 |
| `test_story.py` | 單元測試：`python -m unittest tools/cut_content/test_story.py` |
| `story_verify.py` | 票 25.14 自己逐條下的歸屬（S14–S17、`OWNERS` 裡的 S13b／S15、第 30 章歸 S4 的四條、S5 區塊的開頭、`16:0x00` 的例外）的獨立驗證：`show` 印出單一項主張（判定 agent 看到它的唯一途徑）、`mechanical` 以字串比對直接判定「與顯示中的條目逐字相同」的項目、`pending`、`check`（判定檔的 gate）、`rescan`、`report`（收尾報告與判定彙整到 `devlog/runs/<date>-story-verify-*.json`）。判定檔在 `workspace/story_verify/verdicts/`。主張是從當時的 `OWNERS` 產生的；判定落地後 `OWNERS` 依判定改寫，同樣的指令產生的主張也跟著變，所以 `check` 只對那一次的狀態有意義，定案的判定以 `devlog/runs/2026-09-26-story-verify-verdicts.json` 為準 |
| `verify_story_ticket25_14.js` | 上一列的 workflow：一項一個 agent、每輪 gate、失敗重試一次、回掃、收尾報告。`Workflow({ scriptPath: "tools/cut_content/verify_story_ticket25_14.js", args: { date: "YYYY-MM-DD" } })` |
| `test_story_verify.py` | 單元測試：`python -m unittest tools/cut_content/test_story_verify.py` |
| `units_media.py` | `units` 主題（票 25.11）的素材產生器：頭像（`FACE.CEL`）、12 格棋子圖示排成一張（`ICON.CEL`）、戰鬥動畫 `STAND`／`ACT` 的逐格 PNG 與總覽圖、動畫內嵌音效的 WAV。頭像與圖示用 `FDE.PAL`，戰鬥動畫用 `FIGHT.PAL`；動畫每格照 `saf_decode.compose_frame` 合成在 320×200 的戰鬥畫面上，再裁到整段動畫共同的最小框 |
| `test_units_media.py` | 單元測試：`python -m unittest tools/cut_content/test_units_media.py`（有原版遊戲檔時另跑一次完整產生，檢查檔名與兩次產生逐 byte 相同） |
| `battle_assets_media.py` | `battle_assets` 主題（票 25.13）的素材產生器：戰鬥背景一張 320×200 PNG；永遠不載入的動畫每格一張 PNG、一張總覽圖與它帶的全部音效 WAV；會播的動畫只輸出不響的那幾段音效；容器裡的 WAV 原樣複製。一律用 `FIGHT.PAL`，每格照 `saf_decode.compose_frame` 合成在整個 320×200 畫面上、不裁切；總覽圖直接用 `saf_decode.write_filmstrip`（排版與寫檔一步完成，輸出同樣是決定性的）。哪個項目產出哪些檔只由 `plan()` 決定 |
| `test_battle_assets_media.py` | 單元測試：`python -m unittest tools/cut_content/test_battle_assets_media.py`（有原版遊戲檔時另跑一次完整產生，檢查檔名、格數、取樣率與原樣複製） |

## 子命令

```
python tools/cut_content/cut_content.py index
python tools/cut_content/cut_content.py check
python tools/cut_content/cut_content.py media <主題> [--game DIR]
python tools/cut_content/cut_content.py verify-media [<主題>...] [--game DIR]
```

- `index`：從五個主題檔的條目標題列與 `分類：` 行產生 `cut_content/_index.md` 的總表，蓋掉表頭相同的舊表。
- `check`：結構閘門。條目標題與分類、編號字首符合主題、編號不重複、同一編號不同時是條目與排除項、總表與主題檔同步、相對連結都存在、沒有引用 `workspace/`、每個素材檔的名稱照規則、屬於一個條目且被主題檔引用。`media/cdda/` 不檢查。每張票改完 `cut_content/` 都要跑到 `OK`。
- `media`：從原版遊戲檔重生一個主題的素材，整個 `cut_content/media/<主題>/` 換成新產生的內容，舊檔不留。
- `verify-media`：重生到暫存資料夾，逐 byte 比對版控中的素材，列出多出、少了與內容不同的檔。「從遊戲檔重生結果相同」由它證明。

`--game` 預設是 `fdps_game_files/`（原版遊戲檔，不進版控）。

## 註冊素材產生器

產生器是 `(out_dir, game_dir) -> None` 的函式，把該主題的全部素材寫進 `out_dir`，登記在 `GENERATORS[<主題>]`。主題鍵是 `code`、`units`、`items`、`battle_assets`、`story`。

- 讀遊戲檔用 `read_game_file(game_dir, "FACE.CEL")` 與 `read_vfs_member(game_dir, "FIGACT.VFS", "STAND087.SAF")`，不要讀 `workspace/vfs_dump/`，那是可刪的中間產物。
- 寫檔用 `write_png(path, width, height, rgba_rows)` 與 `write_wav(path, sound)`，分別轉交 `cel_decode` 與 `saf_decode` 的寫出函式；兩者的輸出是決定性的，重生才會逐 byte 相同。
- 檔名照 [`cut_content/_index.md`](../../cut_content/_index.md)「素材」的規則，`check` 會擋下不合規的名稱。
- CD 音軌的 WAV 寫到 `cut_content/media/cdda/`（`.gitignore` 排除），不經 `media` 子命令的整夾替換：`python tools/cut_content/story.py cdda [--disc1 CUE] [--disc2 CUE]` 以 `cd_scope` 的 `.cue` 讀取器找音軌起訖，把 CD-DA 取樣原樣寫成 44.1 kHz、16 bit、立體聲的 WAV。
