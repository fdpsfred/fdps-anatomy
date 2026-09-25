# data_skill — 遊戲資料查詢 skill 的資料集建置

從遊戲檔產生 `.claude/skills/fdps-data/` 這個查詢 skill 的兩份資料集：`fdps_data.json`（九張表）與 `fdps_text.json`（66 個文字區塊的每一條）。輸出進版控，因此不寫在 `workspace/` 而是寫在 skill 自己的資料夾裡。

```
python tools/data_skill/build.py [--game DIR] [--out DIR] [--no-chapter-page-gate]
python -m unittest tools/data_skill/test_data_skill.py
```

`--game` 預設 `fdps_game_files/`，`--out` 預設 `.claude/skills/fdps-data/`。建置直接從 `MISC.VFS`、`FIELD.VFS`、`FIELD1.VFS`、`FIELD2.VFS`、`ICONANI.VFS` 解，不讀 `workspace/vfs_dump`。

## 每張表從哪裡來

| 表 | 數值 | 名稱 | 與知識庫的對照 |
| --- | --- | --- | --- |
| `item`、`spell`、`class`、`character` | 本工具自己解 `MISC.VFS` 的六個成員 | 知識庫 `assets/` 的表 | 人手寫的表（物品、法術、職業、人物的出場屬性與升級成長）由本工具逐列拿數值欄對同編號的 record；每人每型態的法術表是 `data_tables` 產生的，由 `data_tables.check` 對 |
| `character` 的轉職路線與部署 | [`data_tables`](../data_tables/_index.md) 的 `Game` | 同上 | `data_tables.check` |
| `enemy`、`race`、`shop`、`use_effect` | `data_tables` 的 `Game` 與 `USE_EFFECTS` | `FDETXT00` | `data_tables.check` |
| `chapter` | [`chapter_docs/chapter_facts.py`](../chapter_docs/_index.md)，加上 `chapter_docs/judgements/` 的逐章判定 | 各章文字區塊的表頭 | 章節頁產生區塊的重新產生比對 |
| 文字 | `text_decode` 解、`chapter_facts` 逐行呈現；讀取端取自 [`global_text`](../global_text/_index.md) 的掃描、`chapter_facts` 的掃描加判定、過場腳本的追蹤 | — | `global_text` 兩頁的重新產生比對 |
| 文字的 `cut_content/` 歸屬 | [`cut_content/story.py`](../cut_content/_index.md) 的 `never_shown_text` 與 `OWNERS`，條目標題取自 `cut_content.collect` | — | `story.py` 的歸屬閘門與 `story.md` 產生區塊的比對 |

原則是：知識庫裡人手寫的表（物品、法術、職業、人物的出場屬性與成長）由本工具逐列對照；其他工具產生的表不解析它們的 Markdown，而是在同一份資料上跑那個工具自己的閘門。兩條路的結論一樣——資料集與知識庫說的是同一件事——但後者不會因為表格排版而讀錯。

任何一處對不上，建置就中止、兩個檔都不寫，並逐條印出是哪一列、哪一個閘門。文字資料集另有一條：每一條的狀態必須是 `shown`、`never_shown`、`empty` 之一，找不到讀取端又沒有歸屬的條目會讓建置失敗。

`chapter_facts` 會快取第一次載入的資料，所以一個行程只能對一份遊戲檔建置；換另一份遊戲檔要另開行程。

`--no-chapter-page-gate` 只跳過章節頁（`chapters/chNN.md` 的產生區塊與 `chapters/_index.md` 的表）那一道，給章節頁正在改版、還沒重新落地的時候用；這樣產生的資料集在 `fdps_data.json` 的 `chapter_page_gate` 記成 `false`，章節頁落地後要不帶這個旗標重跑一次。

## 攻略站的歧異

攻略站與資料檔已查明的歧異以常數寫在腳本裡（`DISCREPANCIES`），隨資料集一起輸出，查詢時會印在該筆下面。這份清單與 [`guide_offsets/crosscheck.py`](../guide_offsets/_index.md) 的 `ACCEPTED` 是同一組事實的兩份副本：那支腳本比對的欄位一旦冒出新的不一致就會失敗，所以在**它涵蓋的欄位範圍內**這份清單不會遺漏；它沒有比對的欄位（物品類型、使用對象等只存在於 record 的欄位）則沒有這層保護，往 `ACCEPTED` 加一筆時要一併加到這裡。

建置本身能擋的是清單過期：每一筆歧異的資料檔那一側都會回頭對 record 驗證，值改了就中止，不會留下一個掛錯數字的「攻略站寫…」提示。

## 只收有把握的欄位

人物的出場屬性只給程式會讀到的索引：入隊的 `00`–`0B` 與部署記錄讀到的非隊員角色（`0C`、`0D`、`0E`、`23`、`24`–`27`、`3B`）。`assets/characters.md` 的出場屬性表列的正是這一組，建置時拿 `data_tables` 算出的讀取端集合對它，多一列少一列都中止。其餘位置的 byte 是沒有讀取端的複本，轉職型態的索引拿到的就是這種複本，掛上去會變成看似合理的假資料，所以留空。

同理，沒有解讀出對照表的代碼欄位（物品類型、使用對象等）只輸出原始值，不附推測的名稱；使用效果代碼的說明則是 `data_tables` 從 `src/item.c` 轉錄的那一份。

波次會不會部署、文字由誰顯示、為什麼不會顯示，資料本身判斷不了，一律取自已落地的判定（章節頁的 `judgements/chNN.json`、`story.py` 的 `OWNERS`），不在本工具另下判斷。判定缺一章、或判定與該章資料對不上（`chapter_facts.validate_judgement`：波次漏判、文字條目沒有讀取端也沒有列為不會顯示），建置就中止，所以出貨的資料集裡每個波次都有結論、每條文字都有狀態。

## 對應的前作成果

前作 `fd2-anatomy/.claude/skills/fd2-knowledge/`（`query.py` 與 `build_index.py`）的指令涵蓋面沿用：一章的敵人／寶物／事件分段查詢（`chapter N --enemies` 等）與全文搜尋（`grep`）。它的資料是從攻略站 HTML 解析出來的，FDPS 依 [ADR-0006](../../docs/adr/0006-guide-as-mirrored-text-not-parsed-data.md) 不解析攻略站，所以程式不沿用：章節資料改取自地圖與過場腳本的解碼，全文搜尋的對象改成遊戲自己的 66 個文字區塊，並多了「這條文字為什麼不會顯示」這種前作沒有的查詢。

## 檔案

| 檔案 | 用途 |
| --- | --- |
| `build.py` | 產生兩份資料集；`build()` 回傳 `(data, text)`，`write()` 寫檔 |
| `test_data_skill.py` | 單元測試：資料集的內容（`build.build`）與查詢指令的輸出（`query.main`），預期值取自知識庫的表與逐章判定 |
