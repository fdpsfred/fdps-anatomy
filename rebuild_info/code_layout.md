# 原始碼編排 — 哪個符號在哪個檔

**驗證對象**：`src/` 的檔案切分。每一支 `pool_fdps` function 與每一個遊戲自有的全域符號屬於哪個 `.c`／`.h`，以此檔為唯一正典；逐項對照表在 [`tools/code_emit/data/routing.json`](../tools/code_emit/_index.md)（機器讀）與同目錄的 `routing.md`（逐檔清單）。

符號怎麼命名由 [`naming.md`](naming.md) 擁有，emit 一支 function 的流程由 [`emit_pipeline.md`](emit_pipeline.md) 擁有，本檔只回答落點。

## 為什麼要先排好

emit 是序列的，一次一支 function，每支各自 commit。檔案落點如果邊做邊決定，第一百支落地時才發現該和第十支同檔，就得回頭搬動已經通過審查並 commit 的程式碼——而 reviewer 判斷「本輪改了什麼」靠的是工作區相對 `HEAD` 的 diff，搬檔會把大量不相干的改動塞進下一支的審查範圍。先排好，emit 期間就不必再問這個問題。

## `src/` 是平的

沒有子資料夾。理由是建置腳本 `tools/code_emit/build_emit.py` 以 `src/*.c` 收集編譯清單，並把全部原始碼攤平暫存到 DOS 端的 `C:\SRC`——子資料夾在那裡不存在，同名檔會互相覆蓋。因此 basename 在整個 `src/` 內必須唯一，且 ≤ 8 字元（[`naming.md`](naming.md) 的檔名條）。

## 分組的依據

依**子系統與呼叫關係**分組，不依位址、不依字母。判準依序是：

1. **語意**——function 在做什麼，由票 15 的名稱與 plate comment 給出。
2. **呼叫關係**——caller 與 callee 儘量落在同一個檔，跨檔呼叫走該檔的 `.h`。
3. **共用的全域**——一起讀寫同一組狀態的 function 屬於同一個檔，那個檔就是那組狀態的擁有者。

三者衝突時以語意優先，並在 `build_routing.py` 的 `OVERRIDES` 記下該支 function 的理由。**名稱不是依據**，只是語意的線索：`fdps_flash_units_in_color` 名字裡有顏色但屬於戰鬥指示器、`fdps_map_find_chest_cell` 名字裡有 map 但只服務 AI，這類都以實際用途歸檔。

## 行數預算

**每個檔的預估行數不超過 1000。** 這不是程式的性質，是模型讀寫的效率界線：超過之後每次改一行都要把整個檔讀進 context。

預估值的來源是 **Ghidra decompiled code 的行數**，由 `tools/code_emit/DumpRoutingInputs.java` 產生。全部 514 支合計約 50,000 行，分成 86 個 `.c`。這是估計不是保證——實際 emit 出來的 C 會偏離，處置規則見下面「超標了怎麼辦」。

## 資料符號歸誰

判準是**誰讀它**，與 function 的 pool 判定同源（[`naming.md`](naming.md)）：

| 情況 | 落點 |
| --- | --- |
| 只被一個目標檔的程式碼讀 | 該檔。共 97 個 |
| 被兩個以上的目標檔讀 | `gamedata.c`。共 135 個 |
| 章節四張 dispatch 表 | `chapter.c`，見下面的例外 |

`gamedata.c` 不是雜項桶，入場條件是「跨檔共用」這個事實本身。把它集中還有第二個好處：未初始化全域的擺放順序與相鄰關係在 Watcom 下不保證（[`pitfalls.md`](pitfalls.md) 的 B 類），全部落在同一個 translation unit 至少讓那個順序是一份可讀的清單，而不是散在七十幾個檔裡的湊巧。

**例外是 `data_fdps_chapter_*_handler_table` 四張表。** 照「誰讀它」會把它們拆到三個不同的檔、其中一張還單獨落在 `main.c`，但它們指向的 handler 分佈在六個章節檔裡，是同一組 dispatch 介面。四張一起放進 `chapter.c`。

## 不 emit 的東西

Ghidra 必須給名字、但重建版**不能定義**的符號，共 159 個。共同點是編譯器會從我們給它的 C 自己產生一份，我們再定義一次就會多出一個原版沒有的符號。它們在 `routing.json` 的 `skipped` 區，不進票 23 的工作清單。

| 類 | 數量 | 為什麼不 emit |
| --- | ---: | --- |
| `<inline:string-literal>` | 62 | 字串字面值不是具名全域，它回到使用它的那句敘述裡 |
| `<inline:local-array-initialiser>` | 87 | 函式內區域陣列初始值的唯讀副本，回到擁有它的那支 function 的區域宣告裡 |
| `<compiler:switch-table>` | 7 | `switch` 的跳躍表，由 `switch` 敘述本身產生 |
| `<compiler:fp-constant>` | 3 | 浮點常數池，回到使用它的算式裡的字面值 |

每一筆都記著它屬於哪一支 function（`carried_by`）與那支 function 的檔（`carried_in`），因為它的內容要寫在那支 function 的body 裡。

`pool_crt` 與 `pool_ail` 的 function 與資料不在本表內：它們是連結進來的既有程式庫，落點由 [`ail_link.md`](ail_link.md) 與 [`libs/`](../libs/_index.md) 決定。

## 標頭檔

| 檔 | 內容 | 誰 include 它 |
| --- | --- | --- |
| `X.h`（每個 `X.c` 一個） | `X.c` 的公開 function 原型，含各自的 `#pragma aux` calling convention 宣告；以及 `X.c` 定義的全域的 `extern` | 呼叫 `X.c` 的檔 |
| `fdpstype.h` | 23 個遊戲 struct 的定義（票 17 的產物），不含任何 function 宣告。**產生物**：`tools/code_emit/gen_types.py` 從 `ghidra_snapshot/data_types.txt` 產生，連同逐欄檢查偏移的 `tests/fdpstype.c`。改佈局要改 Ghidra 再重跑，不手改 | 需要那些型別的檔 |
| `gamedata.h` | `gamedata.c` 定義的 135 個共用全域的 `extern` | 讀那些全域的檔 |

**`extern` 的擁有者就是定義它的那個 `.c` 的 `.h`，只有一個。** 任何檔都不得自己寫一份 `extern`——那樣的宣告不會跟著定義一起改，型別一旦調整就是兩份不一致的真相，而連結器不會抱怨。

票 22 的 emit 期間，擁有者的 `.c` 常常還不存在（資料是票 23 的產物，`gamedata.c` 尤其）。這不改變規則：**先建出擁有者的 `.h`、把 `extern` 寫在裡面**，該 `.c` 之後補上定義。宣告的落點由誰擁有那個符號決定，不由誰先寫到它決定。

程式庫的型別（`FILE`、`tm`、`REGS`、`SAMPLE` 等）來自 Watcom 與 AIL 的真標頭，不進 `fdpstype.h`；理由與「程式庫函式就叫程式庫的名字」同源（[`naming.md`](naming.md)）。

## 超標了怎麼辦

實際 emit 出來的行數與預估不同是常態。處置規則：

- **還有 function 排在該檔、而它已超過 1000 行——拆。** 在寫下一支之前拆，不要先寫完再搬：搬動已 commit 的程式碼會污染下一支的審查 diff。
- **該檔已經沒有 function 排在後面，只是收尾時超標——不動。** 把審查過的程式碼搬來搬去只為了滿足一個估計值，換不到任何東西。
- **拆分依內聚的子功能，不照字母也不照位址。** 拆出來的每個檔要能用一句話說出它負責什麼；說不出來就代表切點選錯了。

拆檔與任何落點更正，都改 `tools/code_emit/build_routing.py` 的判定表，重跑腳本重新產生 `routing.json` 與 `routing.md`，不手改產出檔。程式碼的搬移、重新產生的路由表、本檔的檔案表，三者進同一個 commit。

**拆完之後，手上那份工作清單就作廢了**——它是用舊路由算的，照著跑會把 function 寫進路由已經不再指名的檔。正確的處置是重新問一次 `next_batch.py`，不是把整批收掉：拆檔發生在檔案跨過預算的時候，而那正是最大的那些 function 陸續落地的時期，所以它會**成群出現在後期批次**而不是偶發。實測 `t22-08` 就是在第二支撞到拆檔、剩下 98 支一支沒動。

`next_batch.py` 以 `routing.json` 為工作清單的名冊，`emit_state.json` 只記進度；兩邊對某支 function 的目標檔不一致時它報錯而不是二選一，因為那正是「同一支 function 被寫進兩個檔」的前兆。

## 檔案表

86 個 `.c`，各自配一個同名 `.h`。逐檔的 function 清單與行數在 `tools/code_emit/data/routing.md`。

### 戰鬥地圖 AI

| 檔 | 負責 |
| --- | --- |
| `mapai.c` | 單一 actor 的行為分派與移動決策 |
| `aiscore.c` | 攻擊／道具／法術的候選評分 |
| `aitarget.c` | 目標收集（範圍、直線、區域）與反擊可行性判定 |
| `aiact.c` | AI 決定之後的動作執行：移動攻擊、用道具、施法 |

### 地圖與移動

| 檔 | 負責 |
| --- | --- |
| `movegrid.c` | 移動格網：重置、ZOC 標記、洪水填充、路徑追蹤 |
| `maptile.c` | 地圖單格資料的存取與觸發事件的套用 |
| `walk.c` | 四方向逐格行走動畫與路徑播放 |
| `mapcur.c` | 地圖游標：繪製、資訊面板、選格迴圈、移動動畫 |
| `mapdraw.c` | 場景圖層組合與地圖單位繪製 |
| `overview.c` | 戰場全覽畫面與其縮放繪製 |

### 戰鬥流程

| 檔 | 負責 |
| --- | --- |
| `btlturn.c` | 回合與階段引擎：敵方／NPC／玩家階段、回合推進 |
| `btlmenu.c` | 戰鬥中的系統選單：目標、存檔、讀檔、離開，以及開啟它的外層選單 |
| `btlact.c` | 單位在自己回合開的行動指令環（攻擊／法術／道具／待命），以及它第四個命令所做的格子搜尋。`btlmenu.c` 原本三支同檔，寫到 1038 行、外層系統選單還沒落地，於是照兩個 caller 的界線切開：系統選單由玩家階段迴圈開啟、並帶著存檔臂重建的整份 FDE.SAV 影像，行動指令環則由單位回合逐一開啟，而搜尋指令是那個環的第四個命令、沒有別的呼叫者 |
| `btlend.c` | 勝敗條件判定與結果視窗 |
| `combat.c` | 戰鬥演出的驅動、命中判定與背景滑入 |
| `cmbblow.c` | 一次揮擊的演出 |
| `cmbspell.c` | 法術戰鬥場景的演出 |
| `death.c` | 死亡演出與死亡腳本的收集、執行 |
| `deploy.c` | 出擊單位陣列的建立與各波敵人的部署 |

### 單位

| 檔 | 負責 |
| --- | --- |
| `unit.c` | 單位記錄的核心存取、查找與旗標 |
| `unititem.c` | 單位的道具欄與裝備 |
| `unitstat.c` | HP／MP、狀態異常、經驗值與升級 |
| `unitatk.c` | 單位攻擊、命中結算與待命 |
| `table.c` | 十個唯讀資料表的 record accessor |
| `roster.c` | 隊伍名冊：加入、回寫、復活、能力重算 |

### 法術與道具

| 檔 | 負責 |
| --- | --- |
| `spell.c` | 施法效果的套用與施法演出 |
| `spellmnu.c` | 法術清單 UI 與戰鬥中的法術指令 |
| `item.c` | 道具效果的套用與戰鬥中的道具選單 |

### 章節

| 檔 | 負責 |
| --- | --- |
| `chapter.c` | 章節框架：狀態重置、標題卡、四張 dispatch 表 |
| `chinit1.c` / `chinit1b.c` / `chinit2.c` / `chinit2b.c` | 第 1–10 章／第 11–15 章／第 16–24 章／第 25–30 章的 init handler |
| `chinit1b.c` | 第 11–15 章的 init handler。`chinit1.c` 原本涵蓋第 1–15 章，寫到第 10 章就已經 1024 行——每支 handler 都要交代地圖宣告幾個 player slot、過場腳本自己部署了誰、以及 roster 加人排在重建之前有沒有差別，這份說明與函式本體只有幾行呼叫無關——於是趁第 11 章落地前把後五章切出來自成一檔；字母後綴的理由同 `chevt2b.c` |
| `chinit2b.c` | 第 25–30 章的 init handler。`chinit2.c` 原本涵蓋第 16–30 章，寫到第 24 章就已經 1108 行、後面還排著六支，於是照「這支 handler 有沒有人入隊」的界線切開：全遊戲最後一個會呼叫 `fdps_roster_add_character` 的 init handler 就是第 24 章的，第 16–24 章那九支的說明重心都在 roster 與地圖 player slot 數的對帳，第 25–30 章則是結局前的收束，六支全是同一個四呼叫的平版形、彼此只差過場腳本的檔名。字母後綴的理由同 `chevt2b.c` |
| `chpost1.c` / `chpost2.c` | 同上兩段的 post-action handler |
| `chend1.c` / `chend2.c` | 同上兩段的 end handler |
| `chevt1.c` … `chevt6.c` | 章節腳本事件 handler，依章號切段；切點不平均，因為第 8 章一章的四支 handler 就抵得上第 9–14 章的總和 |
| `chevt2.c` | 第 8 章的四支事件 handler：回合排程、守衛陣亡、村民逃出。`chevt2.c` 原本涵蓋第 8–14 章，第 8 章的第四支還沒落地就已經 1106 行，於是照上一列那句話的界線切開，第 8 章一章自成一檔 |
| `chevt2b.c` | 第 9–14 章的事件 handler，即上一列切出去的另一半；不叫 `chevt7.c` 是因為 `chevt6.c` 已經是第 28–30 章，用字母後綴才能讓檔名維持章序 |
| `chevt5b.c` | 第 26、27 章的事件 handler。`chevt5.c` 原本涵蓋第 24–27 章，光第 24、25 章的四支就寫到 1113 行，於是把後兩章切出來自成一檔，字母後綴的理由同 `chevt2b.c` |
| `icon.c` | ICON 腳本直譯器與它的指令實作（過場演出） |

### 村莊與商店

| 檔 | 負責 |
| --- | --- |
| `village.c` | 村莊階段主迴圈、招牌選單、行走與縮放動畫、金錢顯示 |
| `vilmenu.c` | 隊員選擇與道具的販售／轉移／裝備迴圈 |
| `vilshop.c` | 教會、武器店、隱藏選單三個畫面：指令列全部通往隊伍共用的櫃檯 |
| `vilbar.c` | 酒館畫面——指令列是存檔、讀檔與離開遊戲——以及它進場時開的抽獎 |
| `shop.c` | 商店的選貨與購買流程 |
| `shopdraw.c` | 商店清單與隊員列的繪製 |
| `church.c` | 轉職流程與候選人選擇 |

### 介面

| 檔 | 負責 |
| --- | --- |
| `menu.c` | 環狀指令選單與共用的選單開啟／游標／關閉原語 |
| `statwin.c` | 單位狀態視窗的外框：底圖載入、四片滑入滑出動畫、開窗與關窗 |
| `statunit.c` | 狀態視窗裡的單位：數值面板、道具欄清單，以及開著時的走路／待機動畫與輸入等待 |
| `gauge.c` | 各種量表的繪製與戰鬥量表的擺位 |
| `msgwin.c` | 訊息視窗、人像載入繪製、二選一提示 |
| `indicat.c` | 戰場上的浮動指示器佇列（傷害數字、MISS、CURE、閃色） |
| `text.c` | 文字與數字的繪製、1bpp 字形 blit |
| `save.c` | 存檔／讀檔畫面與兩者共用的存檔格游標迴圈 |
| `savepnl.c` | 存檔／讀檔頁面的組版與單格摘要面板的繪製 |
| `savefile.c` | FDE.SAV 本身：把存檔讀回遊戲狀態，以及守護映像的檢查碼與 XOR 加解密 |

### 繪圖

| 檔 | 負責 |
| --- | --- |
| `blit.c` | 矩形 blit 原語：透明、鑲嵌、著色、混合、旋轉縮放 |
| `sprite.c` | sprite 原語：composite sprite、tilemap、CEL 展開與 blit |
| `rle.c` | RLE 基本 blitter：直通、縮放、水平／垂直鏡射、跳列 |
| `rlerot.c` | RLE 旋轉與旋轉縮放 |
| `rlecolor.c` | RLE 調色盤重映與換色 |
| `rleblend.c` | RLE 半透明與著色 |
| `palette.c` | 調色盤暫存器與查找表 |
| `palcycle.c` | 場景與 UI 的調色盤循環動畫 |
| `transit.c` | 畫面轉場：方塊、滑動、隨機塊、縮放 |
| `anim.c` | VFS／SAF 動畫的播放與回合旗幟 |

### 資源與平台

| 檔 | 負責 |
| --- | --- |
| `vfs.c` | VFS 容器的開啟、查表與讀取 |
| `saf.c` | SAF 動畫格的解析與播放 |
| `rsrc.c` | 章節資源的載入／釋放與 CEL sprite 快取 |
| `audio.c` | AIL 之上的音效與 WAV 播放、計時器 |
| `cd.c` | CD 裝置層：DOS 緩衝、device request、ioctl、狀態 |
| `cdaudio.c` | CD 音軌播放、暫停、續播、位置查詢 |
| `cdtoc.c` | 光碟與音軌資訊、MSF 換算 |
| `dpmi.c` | DPMI 的 DOS 記憶體配置與區段鎖定 |
| `keybd.c` | 鍵盤 ISR、佇列與掃描碼讀取 |
| `main.c` | 進入點、全域資源載入與釋放 |
| `title.c` | 標題畫面、demo、game over、FMV 播放 |
| `ending.c` | 片尾：roster 每個成員一張動畫卡與收尾的 End 影片。原本與標題畫面同屬 `title.c`，但四支落地後已 1117 行、`fdps_title_screen`還排在後面，於是把片尾這段自成一檔——它是唯一不屬於「標題與結束畫面」那組、由章節結局叫起來的前台演出 |
| `gamedata.c` | 跨檔共用的全域狀態，唯一的資料專用檔 |
