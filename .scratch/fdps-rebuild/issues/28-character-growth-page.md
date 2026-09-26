# 28 — 角色屬性數值比較網頁

**What to build:** 一個自足的互動網頁，列出每位可加入角色從入隊等級到最高等級、每一級的 HP／MP／AP／DP／DX 成長範圍，涵蓋每一條轉職路線，可排名、疊圖比較、看單一角色明細。放在 `docs/character-stat-comparison/`，由 GitHub Pages 發佈。

**前作參照（照 CLAUDE.md，能沿用就沿用）：** `fd2-anatomy/tools/growth_table/`（`gen_growth.py`、`page_template.html`、`build_page.py`、`spot_check.py`、`verify_js_browser.js`、`_index.md`）與發佈檔 `fd2-anatomy/docs/character-stat-comparison/fd2_growth_tables.html`、`fd2-anatomy/docs/README.md`。網頁的三個分頁（屬性排名、成長曲線比較、角色明細）的功能涵蓋面與驗證方式照前作；**資料層與公式一律取自 FDPS 自己的來源**，前作的公式不能照搬。

**介面改良：** 前作的版面與視覺是起點，不是終點。動手做頁面之前先載入 `frontend-design:frontend-design` skill，照它的方法重新決定視覺方向（整體風格、字體、配色、版面與互動細節），改良前作的介面，避免做成套版的預設樣子。功能涵蓋面不得少於前作的三分頁；圖表部分仍照 `dataviz` skill 的配色與可讀性規則（淺／深主題都要可讀、色盲友善）。改良的決定與理由寫在 `tools/growth_table/_index.md` 的「網頁設計」一節，並列出與前作介面的主要差異。

**FDPS 端的資料與公式來源：**

- 表：`FRIAPRDA.DAT`（基礎值）、`FRILEVUP.DAT`（成長範圍）、`RANKUP.DAT`（轉職路線）；讀取直接 import `tools/data_tables/data_tables.py`，不另寫解析。
- 公式：`program_info/battle.md`（出場與升級、成長上限的 exclusive 界線、成長值相等時不抽亂數）、`assets/characters.md`（加入、轉職路線、教會規則、勇者徽章路線）、`program_info/village.md`（教會轉職）、`rebuild_info/pitfalls.md` 相關列、`program_info/known_bugs.md`（例如第 2 條蓋亞升上 40 級時經驗餘數歸零）。
- 驗證：`src/unitstat.c`（升級）、`src/church.c`（轉職）、`src/roster.c`（入隊）為最終依據；攻略站鏡像 `docs/guide/` 只作對照（`assets/characters.md` 已記錄英雄的 HP 下限與攻略站不符等差異）。

**已知要注意的：**

- `docs/` 在本專案不是空的發佈目錄：它放著 ADR、agent 規範、研究筆記與**攻略站鏡像 `docs/guide/`**。GitHub Pages 以 `/docs` 為來源時會把這些一起公開。啟用 Pages 是開發者的動作，本票只產生頁面與說明，不代為設定或推送；`docs/README.md` 要寫明這個影響與可選的做法（例如另開 `gh-pages` 分支只放網頁）。
- repo 目前沒有設定遠端；發佈網址由開發者決定後補。

**Blocked by:** None — can start immediately

**Status:** ready-for-agent

- [ ] 產生器從遊戲資料推導全部可加入角色 × 轉職路線的逐級成長（最小／最大），與 `src/` 逐項獨立重算 0 誤差
- [ ] 自足單檔網頁（資料、CSS、JS 全內嵌，零外部引用），三分頁的功能涵蓋面照前作，淺／深主題
- [ ] 以 `frontend-design:frontend-design` skill 改良介面，設計決定與相對前作的差異記在 `tools/growth_table/_index.md`
- [ ] 網頁內 JS 的計算結果與 Python 產生器全量比對 0 誤差
- [ ] `docs/character-stat-comparison/` 發佈檔、`docs/.nojekyll`、`docs/README.md`（含上面的公開範圍說明）；`tools/growth_table/_index.md`、`tools/_index.md`、`README.md` 同步
- [ ] 發現「照直覺寫就會與原版不同」的事寫進 `rebuild_info/pitfalls.md`；devlog 一篇
