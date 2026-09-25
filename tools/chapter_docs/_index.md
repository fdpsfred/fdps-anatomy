# chapter_docs — 30 章章節檔

產生並驗證 [`chapters/`](../../chapters/_index.md) 的 30 份 `chNN.md` 與 `chapters/_index.md`（票 25.8）。

一份章節頁是「agent 寫的敘述」加上「本工具從資料產生的區塊」。能從出貨資料與 `src/` 直接讀出的東西——部署記錄、寶物、回合與格子事件、過場腳本逐步內容、整個文字區塊的全文與讀取端——一律由產生器寫，夾在 `<!-- chapter_docs:KEY -->` 與 `<!-- /chapter_docs:KEY -->` 之間，不手打、不直接編輯。資料本身判斷不了的三件事由逐章的 agent 判定，寫在判定記錄裡，產生器讀它：

- **波次**：波次 0 以外的每個波次會不會部署、何時、由誰部署。不部署的只寫一行連到 `cut_content/`。
- **文字讀取端**：原始碼掃描找不到讀取端的條目，由什麼顯示。
- **永遠不會顯示的條目**：只寫一行連到 `cut_content/`，不列全文。

判定記錄落地後存在本資料夾的 `judgements/chNN.json`（agent 的判定，無法重生，所以跟著工具進版控），讓落地後的頁面隨時可以用最新的資料與原始碼重新產生並比對。

## 前作參照

前作 `fd2-anatomy/chapters/chapter_NN.md` 的章節頁結構（劇情、加入、敵人、寶物、商店、特殊機制、處理流程、事件、對話）沿用為各節的涵蓋面；前作 `tools/kb_overhaul/gen_ch_encounters.py` 是解 `FDFIELD.DAT` 的，FDPS 的部署、寶物與事件在 `MAPnn.DAT`、文字在 66 個 `FDETXTnn`、還多了過場腳本，格式全不同，所以不沿用程式，只沿用「部署記錄按單位與等級彙總、寶物分地圖與掉落兩類」的呈現方式。

## 檔案

| 檔案 | 用途 |
| --- | --- |
| `chapter_facts.py` | 產生器。`facts <n\|all>` 寫出每章的機械事實（`workspace/chapter_docs/facts/chNN.json`，給 agent 的線索清單）與區塊預覽；`block <n> <key> [--draft]` 印一個區塊；`render-maps` 把 30 張戰場標註圖畫到 `workspace/chapter_docs/maps/`。區塊的鍵：`header`、`deployments`、`treasure`、`village`、`events`、`scripts`、`dialogue` |
| `check_chapter.py` | 頁面的骨架（`--template <n>`）、填區塊（`fill`）與閘門：`--draft` 檢查草稿、`--landed` 另外把落地頁裡的每個區塊與重新產生的內容逐字比對（資料、原始碼或判定一動就抓得到） |
| `land.py` | 落地：判定記錄說 complete、而且填好區塊的頁面過閘門，才寫出 `chapters/chNN.md` 與 `judgements/chNN.json`；不判斷、不改草稿 |
| `index.py` | `chapters/_index.md` 整頁由它組：固定的前言、六張產生的表（章節總表、四張處理表、事件 slot 與共用處理函式、資源對照、額外場景地圖的歸屬、村莊與連戰），以及 agent 起草的「跨章機制鏈」一節。`build` 寫出、`verify` 重組後比對 |
| `collect.py` | 把全部草稿的 meta 收成一份精簡索引，給 workflow 的後段用 |
| `apply_pitfalls.py` | 把逐條判定的踩雷點列寫進 `rebuild_info/pitfalls.md`，插入規則 import 自 `game_mechanics/apply_kb.py` |
| `chapters_ticket25_8.js` | 全自動 workflow：一章一個 agent 起草、每輪落地後跑閘門、回掃、跨章機制鏈、踩雷點逐條判定、run record 與 devlog |
| `test_chapter_docs.py` | 單元測試 |

```
python tools/chapter_docs/chapter_facts.py facts all
python tools/chapter_docs/check_chapter.py --draft 3
python tools/chapter_docs/land.py 3
python tools/chapter_docs/check_chapter.py --landed 3
python tools/chapter_docs/index.py build
python -m unittest tools/chapter_docs/test_chapter_docs.py
```

## 資料來源（全部 import，不另寫解析）

- 地圖、部署、可搜尋格、事件碼層：[`map_decode`](../map_decode/_index.md)
- 過場腳本、它的呼叫端與每一步的地圖／文字／單位：[`cutscene_script`](../cutscene_script/_index.md)
- 文字區塊：[`text_decode`](../text_decode/_index.md)
- 名稱（`FDETXT00`）、`SHOPnn`、`CHAPTER_HAS_NO_VILLAGE`：[`data_tables`](../data_tables/_index.md)
- `src/` 裡 `fdps_draw_text` 的條目運算式怎麼解開：[`global_text`](../global_text/_index.md) 的掃描函式
- function 位址：[`data_emit`](../data_emit/_index.md) 的 Ghidra 快照名稱表
- 四張處理表的內容：`src/chapter.c` 的初值

## 文字讀取端的歸屬規則

每一章頁的「對話」列出本章區塊 `FDETXTnn` 每一條的讀取端，掃描規則寫在 `chapter_facts.py` 的 `GENERIC_READERS`：

- 以 `fdps_chapter_NN_` 命名的函式只屬於第 NN 章；沒有章號的事件處理函式屬於資料裡呼叫它 slot 的章。
- 勝敗視窗、存讀檔畫面的章名：每一章都算。
- 村莊的五個畫面：只算「本章之前有村莊」的章——村莊載入的文字區塊是章節索引 + 1，也就是下一章的區塊。
- 過場直譯器與死亡腳本：條目來自資料，直接讀腳本的追蹤與 `MAPnn.DAT`，不看程式碼那一行。
- 片尾字幕：只算第 30 章。

有 `fdps_draw_text` 讀章節區塊、卻不在上述任何一類的檔案，產生器直接報錯，不猜。
