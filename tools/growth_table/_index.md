# growth_table — 角色屬性數值比較網頁

從遊戲資料推導十二名可加入角色，從加入那一級到等級上限、沿每一條轉職路線、每一級的 HP／MP／AP／DP／DX 範圍（每級都擲最小的下限、都擲最大的上限），做成一個自足的互動網頁：屬性排名、成長曲線比較、角色明細三個分頁。發佈檔在 [`docs/character-stat-comparison/index.html`](../../docs/character-stat-comparison/index.html)，發佈方式與公開範圍見 [`docs/README.md`](../../docs/README.md)。

```
python tools/growth_table/gen_growth.py      # 模型與全量逐級數值 → workspace/growth_table/*.json
python tools/growth_table/build_page.py      # 重產 JSON 並寫出頁面（workspace 預覽＋docs 發佈檔）
python tools/growth_table/build_page.py --check
python tools/growth_table/src_replay.py      # 照 src/ 逐級重播，與產生器逐值比對
python tools/growth_table/verify_js.py       # headless Chrome：頁面 JS 與 Python 逐值比對、三分頁與全部控制項的煙霧測試
python -m unittest tools/growth_table/test_growth.py
```

## 資料流

```
workspace/vfs_dump（FRIAPRDA／FRILEVUP／RANKUP／GETMGTAB／FDETXT00／MAPnn）
  → tools/data_tables.load()                    解碼與名稱，本工具不另寫解析
  → gen_growth.model()   每名角色的參數（頁面內嵌：growth_compact.json）
  → gen_growth.expand()  每種加入等級 × 每條路線 × 轉職等級 20–40 的逐級數值（growth_data.json）
  → build_page.py        注入 page_template.html 的 /*__DATA__*/ → index.html（預覽＋發佈）
```

頁面只內嵌參數（基礎值、成長表原值、路線），逐級數值由頁面 JavaScript 當場算；`growth_data.json` 是兩道比對的標準答案，不進頁面。

## 檔案

| 檔 | 用途 |
| --- | --- |
| `gen_growth.py` | 規則（`gain_range`、`entry_stats`、`base_rows`、`promo_rows`、`level_cap`、`promote_levels`）、角色模型（`model`）、全量展開（`expand`）。每名角色怎麼加入（`JOINS`）與各常數從 `src/` 與知識庫轉錄，見下節 |
| `src_replay.py` | 獨立驗證：把 `fdps_roster_add_character`、`fdps_level_up_apply_stat_gain`、`fdps_unit_award_exp_and_level_up` 的等級判斷、教會的候選與路線判斷、`fdps_church_promote_loop` 的加值逐行轉錄成 16 位元欄位上的逐級重播，`rand()` 換成回答 0 或「範圍 − 1」的選擇器；等級上限、誰能轉職、哪種徽章組合走到哪個型態都由重播自己決定（每種加入等級 × 每個轉職等級 × 徽章的所有組合），再與產生器逐值比對 |
| `page_template.html` | 頁面模板（CSS、JS 全在這裡；`<script id="model">` 是唯一算數值的部分） |
| `build_page.py` | 注入模型、雙輸出；`--check` 確認發佈檔就是現在建置會寫出的內容；`build_preview()` 只寫 JSON 與 workspace 預覽（單元測試用，不動發佈檔） |
| `verify_js.py` | 從建好的頁面原樣切出 `<script id="model">` 與內嵌資料，在 headless Chrome（沒有就 Edge）裡對 `growth_data.json` 逐值比對；另外逐一載入三個分頁、在頁面裡操作每一個控制項（轉職等級 20／30／40、每名角色、加入方式、路線、排序、比較的每個選項與滑鼠停留），有任何未攔截的例外就失敗 |
| `test_growth.py` | 單元測試：規則用手造的記錄，遊戲資料的期望值手抄自攻略站 `docs/guide/fdps/list.txt`（出場數值、40(m)／99(m) 欄＝每級擲最大且 LV40 轉職、轉職建議的徽章），並跑上面三道比對 |

## 寫在程式裡的知識

- **加入等級（`JOINS`）**：照 [`assets/characters.md` 的「加入」](../../assets/characters.md#加入)。加入那一章的地圖部署了同一角色編號的單位時，過關寫回以那個單位蓋過名冊記錄：法蓮娜（LV8）、費塔加（LV15）、瑪麗安（LV20）一定以戰場上的單位留隊；蘭斯洛特（LV2，第 6 回合到場）與珊（LV15，第 7 回合到場）要看到場前有沒有過關，名冊記錄的 LV15、LV10 也是一種加入方式，兩種都列；照一般打法拿不到的名冊版在頁面上標「提早過關」，並在排名說明、比較頁的選項下方與角色明細的加入方式旁用文字說明（費塔加的依據另見 [`chapters/ch08.md`](../../chapters/ch08.md)）。模型裡每種加入等級只帶結構化的欄位（`how`：`roster`、`stays`、`arrives`、`early`，與到場回合），說明文字由頁面組出。產生時逐筆核對部署記錄確實存在（地圖、等級、陣營）且地圖就是那一章的地圖，對不上就失敗。
- **常數**：等級上限 99（肖像編號 9）／40、轉職門檻 LV20 與肖像編號 < 9、轉職後等級 1、三種徽章的物品編號，取自 `src/unitstat.c`、`src/church.c`。
- **路線**：每人不重複的型態，照 `data_tables.offered_routes`（勇者徽章只給蘭迪斯）。某個型態也是不帶徽章會走到的，就「不需徽章」；否則需要它最前面那條路線的徽章。

公式與機制本身不在這裡重述：出場公式在 [`assets/characters.md`](../../assets/characters.md)，升級擲骰、上限與蓋亞的經驗餘數在 [`program_info/battle.md`](../../program_info/battle.md)，教會轉職在 [`program_info/village.md`](../../program_info/village.md#教會轉職)，照直覺會寫錯的地方在 [`rebuild_info/pitfalls.md`](../../rebuild_info/pitfalls.md)。

## 網頁設計

視覺方向依 `frontend-design` 的方法決定，圖表的配色與可讀性依 `dataviz`：五個屬性色與比較圖的八色序列都用它的驗證器在本頁淺、深兩個圖表底色（`#fbfcfd`、`#1a2030`）上驗過，相鄰色的色盲分離度全部過門檻；淺色底下有三色對底色低於 3:1，照規則以圖例、線尾標籤與數值表補足。

**方向：軍師的布陣紙。** 冷灰藍的紙面、靛藍的墨色。不用米白＋琥珀色：那組配色跟遊戲沒有關係，也是生成式網頁最常見的預設。唯一的強調色是印章朱紅，只用在一件事上：**教會轉職**。圖上的轉職線、線上的「轉」字章、逐級表裡的轉職分隔列都是它，其他地方不出現朱紅，看到紅章就知道是轉職那一刻。

**字體。** 標題與角色名用明體（Noto Serif TC → 思源宋體 → 新細明體），呼應遊戲內文字所用的倚天明體；內文用正黑體；數字用 Bahnschrift（Windows 內建的 DIN 系窄體，沒有就退回 DIN Alternate／系統字），表格與座標軸用等寬數字。全部是系統字，頁面零外部引用。

**每個數值都是區間。** 這是本頁的核心：升級是在一段範圍內擲骰，所以頁面到處畫的都是「下限–上限」。排名欄每一列畫一條從 0 起算的區間條，淺色段是下限到上限、粗的一端是目前排序用的那一端；表格每格寫成「下限–上限」（相同時只寫一個）；角色明細的小圖是帶狀（實線上限、虛線下限）。

**轉職等級是全頁的設定。** 標頭的滑桿（LV20–40，預設 40）同時套用到三個分頁：排名的終值、比較圖的轉折點、明細的逐級表。橫軸改成「累計等級」：轉職前是原職業的等級，轉職後的 LV n 接在「轉職等級 + n」，所以不論在哪一級轉職，曲線都是連續的一條，蓋亞的 LV99 也畫在同一條軸上。蘭斯洛特（聖騎士）與珊（法師）加入時的職業就是別人在教會轉職後的職業，比較圖把他們的 LV n 也畫在「轉職等級 + n」，與其他人轉職後的同一級對齊，副標題在選到他們時說明這件事；角色明細只看一個人，小圖仍以他們自己的等級為橫軸。誰算「加入時已轉職」由模型的 `joins_promoted` 決定：加入時的職業是任何人的任一條轉職路線會走到的職業（`gen_growth.promoted_classes`，由 `RANKUP` 推導，不寫死名單）。

**三個分頁。**

- **屬性排名**（預設）：五欄 HP、MP、AP、DP、DX，每欄 24 列（可轉職的九人每條路線一列、蘭斯洛特與珊兩種加入方式各一列、蓋亞一列），每欄各自切換「依上限／依下限」與「高到低／低到高」。窄螢幕改成一次一欄、上方選屬性。
- **成長曲線比較**：左側依加入順序列出每名角色的路線，最多選 8 條；選中的顏色跟著那條路線直到取消，不會因為別條取消而換色。上方一列控制：屬性、上限／下限／範圍、數值表。四條以內在線尾直接標名字與終值。預設先選好四條（蘭迪斯英雄、裘娜狂戰士、蓋亞、蘭斯洛特 LV2）讓圖一打開就有內容。
- **角色明細**：左側名冊，右側角色名（明體大字）、加入等級與方式（有兩種時可切換）、加入時的數值、五個屬性各一張小圖（同時看五項，每條路線一色、轉職等級早於 40 時多一條灰色的「不轉職」）、每個職業每級的成長範圍與轉職當下的加值、逐級數值表（依路線切換，轉職那一列是紅章分隔列，並列出該級學會的法術）。

「數值怎麼算出來的」收在頁尾的摺疊區塊，每個分頁都看得到。深淺色跟系統設定，也可手動切換（記在瀏覽器本機）；分頁記在網址的 `#rank`／`#compare`／`#detail`。版面在 375px 寬的手機上沒有橫向捲動。

### 與前作介面的主要差異

| 前作（`fd2-anatomy/docs/character-stat-comparison/`） | 本頁 |
| --- | --- |
| 米白底＋琥珀色強調、每個區塊一張帶陰影的圓角卡片 | 冷灰藍紙面＋靛藍墨色，平面區塊以細線分隔；朱紅只留給轉職 |
| 標題與標籤用粗黑體與等寬字、全大寫小標 | 明體標題、正黑體內文、DIN 系數字，沒有大寫小標 |
| 排名每欄只能看最大或最小其中一個值 | 每列同時畫出下限到上限的區間，另一端的數字也列出 |
| 轉職固定在 LV40 | 全頁的轉職等級滑桿（LV20–40） |
| 橫軸左右兩半（基礎職／進階職）分開計等級 | 單一的累計等級軸，任何轉職等級都連續 |
| 比較圖的顏色依選取順序，取消一條會讓其他線換色 | 顏色跟著路線直到取消 |
| 比較圖只有圖 | 四條以內有線尾標籤；可展開數值表 |
| 角色明細一次看一個屬性的圖 | 五個屬性的小圖並排 |
| 加入等級只有一種 | 蘭斯洛特、珊兩種加入方式都列；法蓮娜、費塔加、瑪麗安用實際留隊的等級 |
| 逐級表只有數值 | 逐級表標出轉職分隔列與學會的法術 |
| 「推導依據」在比較頁用 JS 搬到右欄 | 固定在頁尾的摺疊區塊 |
| JS 與 Python 的比對要手動貼進瀏覽器 console | `verify_js.py` 在 headless Chrome 裡自動跑，並操作每個控制項 |

## 本機預覽

`build_page.py` 之後直接用瀏覽器開 `workspace/growth_table/index.html`（頁面沒有任何外部請求，`file://` 就能用）。改了 `page_template.html` 或產生器之後要重跑 `build_page.py`，並跑 `verify_js.py`；發佈檔過期時 `build_page.py --check` 與單元測試會失敗。

## 對應的前作成果

前作 `fd2-anatomy/tools/growth_table/`（`gen_growth.py`、`page_template.html`、`build_page.py`、`spot_check.py`、`verify_js_browser.js`）與發佈檔 `fd2-anatomy/docs/character-stat-comparison/fd2_growth_tables.html`。沿用的是三個分頁的功能涵蓋面、「只內嵌參數、JS 當場算」的做法與 JS／Python 全量比對這道驗證；資料層與公式全部改照 FDPS 自己的來源寫，差在：資料直接由 `tools/data_tables` 從遊戲檔解，不經 Ghidra dump；轉職門檻是 LV20，轉職當下加的是新型態成長表的原始上限 byte（前作是擲一次新職業的成長）；等級上限以肖像編號 9 判定；加入等級要看同章地圖的部署。前作的 `spot_check.py`（人工對照用的里程碑表）不沿用，改由 `test_growth.py` 以攻略站數值自動對照。
