# 票 25.16：查詢工具擴充

目標是讓 `fdps-data` skill 能回答各章的敵人、寶物、事件，並能對遊戲全部文字做全文搜尋。開工時的狀態：`build.py` 只收五張表（物品、法術、人物、職業、章節名），`SKILL.md` 叫人去 `assets/` 查敵人、種族、商店、轉職路線。

## 先查前作

前作的對應物是 `fd2-anatomy/.claude/skills/fd2-knowledge/`（`query.py` 757 行、`build_index.py`）。它的指令面很完整：`chapter N --enemies/--treasures/--events`、`grep` 全文搜尋、`enemy`、`portrait`。但它的資料全部是從攻略站 HTML 解析出來的，這條路在本專案被 ADR-0006 封死。所以只沿用指令的形狀：`chapter` 的分段旗標直接照抄命名習慣（`--treasures` 改成單數 `--treasure`，其他照舊），`grep` 改名 `text`，對象從攻略站 markdown 換成遊戲自己的 66 個文字區塊。程式一行都沒搬。

## 第一個踩到的：原本的建置已經壞了

跑舊的 `build.py fdps_game_files/MISC.VFS ...` 當基準，直接失敗：`chapters/_index.md: no table with header ['章號', '章節索引', '標題', '文件']`。25.8 把章節總表改成 `index.py` 產生、表頭多了勝敗條件、「標題」改叫「章名」。舊建置是去讀那張表抄攻略站的章名。

這正是「不要解析別人產生出來的 Markdown」要防的事——表頭一改，下游就斷。章名改從各章文字區塊 `0x01` 取（`chapter_facts.chapter_title`），名稱來源也就從攻略站變成遊戲內文字。副作用是第 12 章之類的名稱會跟攻略站不同（攻略站寫「火神的宮殿（眾神的兵器）」），但這本來就是 `assets/names.md` 在管的差異。

## 章節資料要不要讀章節頁

25.8 還在收尾，章節頁每隔一陣子就會被重新落地。一開始考慮過把 `chapters/chNN.md` 的產生區塊當資料來源——那等於解析 Markdown，而且會隨著 25.8 的改版一起晃，放棄。改成直接 import `chapter_facts` 的原料函式（`battle_map`、`searchable_cells`、`event_slots_used`、`chapter_scripts`、`text_readers`、`transcript`）自己組結構化記錄，判定讀 `judgements/chNN.json`。

`chapter_facts` 把資料路徑寫死在模組變數 `DUMP`（`workspace/vfs_dump`）與 `GAME`。本票要求從遊戲檔現解，又不能改 25.8 正在動的檔，最後的做法是建置開頭先用 `story.dump_tree` 把容器解到暫存樹，再把 `chapter_facts.DUMP`／`GAME` 指過去——它的函式在呼叫當下才讀這兩個全域，而且都是第一次呼叫才快取，所以只要在第一次呼叫前設好就一致。這是對別人模組的全域賦值，程式裡註明了原因；如果 25.8 之後把路徑改成參數，這裡要跟著改。

`_ai_text`、`_record_text` 是 `chapter_facts` 的私有函式，照「import 擁有者、不複製」的規則直接用；`chapter_facts` 自己也是這樣用 `global_text._draw_calls` 的。

## 「逐列對照知識庫的表」怎麼做

舊建置的做法是解析 `assets/` 的 Markdown 表、逐欄對 record。物品、法術、職業、人物的出場與成長是人手寫的表，這樣做沒問題，保留。

新的敵人、種族、商店、使用效果表是 `data_tables` 產生的。第一個念頭是照舊把 `assets/enemies.md` 讀進來逐列比，隨即想到那是在解析別人產生的 Markdown。改成：資料集的記錄從 `data_tables.load()` 的同一個 `Game` 組，再跑 `data_tables.check(game)`——那是它自己的閘門，逐格比對知識庫與產生器。兩邊吃同一份資料，閘門過了就代表資料集與知識庫一致。

同樣的思路套到其他頁：`global_text.build_pages` 重建比對兩張文字頁、`story.ownership_problems` 與 `story.md` 產生區塊的重建比對、`chapter_facts.validate_judgement` 逐章驗判定、章節頁與 `chapters/_index.md` 的產生區塊重建比對（用 `check_chapter.fill`／`regions` 與 `index.build_text`）。任何一道不過，兩個檔都不寫。

章節頁那一道在開工當下是紅的：`check_chapter.py --landed-all` 報 30 章都有 stale 區塊（25.8 正在改判定、頁面還沒重落）。所以加了 `--no-chapter-page-gate`，只跳這一道，並把 `chapter_page_gate: false` 寫進資料集。寫到後段時 25.8 已經落地第一段，工作目錄裡的頁面重新對得上，最後出貨的資料集是帶著完整閘門建的。

## 非隊員角色的出場屬性（25.15 交來的）

決定收。理由：`assets/characters.md` 已經寫明 `0C`、`0D`、`0E`、`23`、`24`–`27`、`3B` 是部署會讀到的，查詢時問「索爾的基礎 AP 是多少」卻得到 `null` 是資料集的缺口，不是謹慎。

但「逐列對照」需要知識庫有一張能對的表。那些數值原本寫在「非隊員角色」一節的散文格子裡（「職業 `03`、LV 10、HP 960、……」）。去解析那種格子就是對人寫的內容寫 parser，放棄。改把這九列加進既有的「出場屬性」表——表頭不變，舊的逐欄檢查直接涵蓋；散文那一節改成指向這張表，只留「與 `0C` 逐 byte 相同」「配備與蓋亞相同」這種表格表達不了的關係。為了不讓表與讀取端再度脫節，建置時拿 `data_tables.Game.base_record_readers()`（入隊的 `00`–`0B` 加上 63 張地圖部署記錄裡小於 `0x3C` 的編號）對表的索引集合，多一列少一列都中止。

順帶發現 `23`、`24`–`27` 的物品欄是 `00 00`，`00` 是岩石。這些角色只經部署建立、物品取部署記錄，所以不會真的帶岩石；寫進了出場屬性表下方的說明。查了 `pitfalls.md` 第 201、202 列已經涵蓋「FRIAPRDA 不是 60 組屬性」「level 欄不是出場等級」，物品欄這點沒有另立一列。

`blank` 的定義跟著改成「升級成長表與出場屬性表都沒列」，`0D`、`0E`、`24`–`27` 因此不再是 blank（舊 `SKILL.md` 第 44 行說的是舊定義）。有內容的人物從 34 變 41。

## 文字資料集

一條文字要回答三件事：全文、誰顯示它、不顯示的話為什麼。來源各有擁有者：全文用 `chapter_facts.transcript`（章節頁的逐行呈現，說話者換成人名），讀取端分三種區塊各取各的（`global_text.scan_readers`、`chapter_facts.text_readers` 加判定、`global_text.scene_refs`），「永遠不會顯示」與歸屬取 `story.never_shown_text` 與 `story.OWNERS`，條目標題用 `cut_content.collect`。

`FDETXT00` 名稱區的讀取端一開始想列出所有以代碼索引的函式，單位名 150 條、每條都掛同一串三十幾個函式，檔案會膨脹好幾倍又沒有資訊量。改成每條只寫一行「單位名：程式以角色編號 + 0x001 取這一條（N 個函式）」。

全文搜尋在比對前把一條的所有行接起來，這樣被 `{br}` 切開的片語（例如敗北條件「蘭迪斯死亡／索爾死亡」）也搜得到。

## 查詢指令的小修

- `find` 一開始會掃進章節記錄，寶物的事件碼、部署的索引跟物品碼混在一起，`find 0xB5` 會冒出一堆不相干的章。章節表從 `find` 排除，改由 `deploy` 與 `chapter` 查。
- `use_effect` 沒有名稱欄，`list` 全部顯示「(未命名)」。改成取說明文字冒號前的那段當標籤。
- `SKILL.md` 的範例 `where enemy hp=1000..` 一筆都沒有——`ENEMYDAT.DAT` 的 HP 是每級係數，最大才六百。換成會有結果的範例。
- 陣營欄用 `%-4s` 對齊中文會歪，改用既有的全形寬度 `pad`。

## 順手更正

`tools/data_tables/_index.md` 寫「有村莊的 20 個 `SHOPnn.DAT`」，實際是 21 個（章節索引 1–29 扣掉沒有村莊的 16、17、21、22、26–29），`assets/shops.md` 的表也是 21 列。

## 審查

實作完用兩個平行的審查 agent 各看一軸（規範、票面）。採納的：

- `query.py` 的輸出訊息原本寫中文（「勝利」「波次」「永遠不會部署」），違反 CLAUDE.md「輸出訊息用英文」。改成英文標籤，資料本身（名稱、判定的說明）照原文。測試裡比對這些字的斷言跟著改。
- `build.py` 還在解析 `assets/characters.md` 的「每人每型態的法術」表——那張是 `data_tables` 產生的，正是本票說不解析的那種。這段是舊建置留下的，拿掉，改由 `data_tables.check` 涵蓋。
- 舊建置自帶一個 VFS 讀取器 `read_entries`，與 `vfs_dump.parse_container` 重複；既然建置已經用 `story.dump_tree` 把容器解開，六張表改從解開的樹讀，讀取器刪掉。
- `tools/_index.md` 的擁有者表漏列：`data_tables`、`cut_content.py`、`check_chapter.py`／`index.py` 都被 import 卻不在表上；`text_decode` 那列反而多寫了本工具（其實是間接用到）。補齊、拿掉。
- `provenance.rule` 寫「名稱一律出自遊戲檔」說過頭：物品、法術、職業、人物的名稱是從 `assets/` 的表讀的，只是那些名稱欄被 `data_tables.check` 對過遊戲內文字。改寫成實際的路徑。
- 來源雜湊漏了 `FIELD1.VFS`、`FIELD2.VFS`（地圖圖層在裡面）。
- 文字狀態除了文件寫的三種，程式還可能產生 `unsettled`、`no_reader`。改成建置時擋掉：出貨的資料集只會有三種。
- 對 `chapter_facts` 全域的改寫，加了「一個行程只建一份」的防呆，第二次換目錄直接失敗，而不是默默吃到快取的舊資料。

沒有採納的：

- 票面審查把種族、商店、使用效果三張表與 `deploy` 指令列為範圍外。這三張表是派工時明講要補的（`SKILL.md` 原本叫人去 `assets/` 查），`deploy` 是「這個敵人在哪幾章出場、會不會真的部署」的直接答案，留著。
- 不部署的波次、沒有格子引用的寶物記錄沒有像文字那樣帶 `cut_content/` 條目編號。那兩類在 `cut_content/` 沒有機器可讀的歸屬表（`story.py` 的 `OWNERS` 只管文字），要加得先有擁有者建表，不在本票。
- 非隊員角色的索引清單同時出現在 `assets/characters.md`、`SKILL.md`、本工具的 `_index.md`——後兩者是使用說明，引用正典的結論；建置本身不寫死清單，而是拿讀取端集合去對表。

## 留下的

- `fdps_data.json` 從約 11,000 行長到 1.1 MB，大部分是 30 章的部署記錄與說明文字。沒有壓縮，理由是 diff 可讀比體積重要。
- 資料集是在 25.8 落地之後、以完整閘門（含章節頁）建的。
