# tools — 工作腳本

每個子資料夾對應一項工作。腳本 self-contained，不另立共用函式庫。例外只有一種：**某項機制或對照表已經有一個擁有者時，需要它的工作直接 import 擁有者的腳本，不複製一份。**理由是複製一份會讓缺陷在其中一份悄悄修不到——例如「掛住被當成跑完」、或名稱對照表其中一份過期。目前的擁有者：

| 擁有者 | 擁有的東西 | import 它的 |
| --- | --- | --- |
| `fdps_build/build_min.py` | 把 DOSBox-X 跑起來的機制：前置檢查、conf 產生、三訊號結束偵測、故障掃描 | 所有在 DOSBox-X 裡建置或執行的工作 |
| `ail_link/link_ail.py` | AIL 的七條 alias 與 SB16 參數 | `code_emit`、`game_build` |
| `code_emit/build_emit.py` | `src/` 的原始碼掃描與反編譯器預設名稱檢查 | `game_build`、`build_gate`、`global_text`（`strip_c`） |
| `data_emit/check_data.py` | LE 映像解析、wlink map 解析、Ghidra 快照的名稱表 | `game_build`、`game_mechanics` |
| 各工作的建置函式 | 各建置目標怎麼建 | `build_gate` |
| `vfs_dump/vfs_dump.py` | `.VFS` 容器解析（`parse_container`） | `cutscene_script` |
| `cutscene_script/cutscene_script.py` | 過場腳本的解碼，以及每一步當下的地圖、文字區塊、單位身分 | 需要過場內容的工作（票 25.6、25.8、25.14 等） |
| `map_decode/map_decode.py` | 地圖的全部檔：`MAPnn.DAT`／`.COD`（`parse_map_dat`、`parse_map_cod`）、圖層（`parse_dtl`、`parse_mpl`、`parse_attr`、`parse_dsc`）、整張地圖的載入與一格的判讀（`load_map`、`tile_info`、`classify_cell`、`searchable_cells`）、全圖算圖（`render_map`） | `data_tables`、`data_skill`；需要地圖、部署記錄或寶物的工作（票 25.2 的 `cutscene_script` 待改、25.8、25.14 等） |
| `text_decode/text_decode.py` | `FDETXTnn.TXT` 的解析、token 分類與文字呈現，以及 `assets/text/glyph_table.json` 的讀取 | `glyph`、`data_tables`；需要遊戲文字的工作（票 25.6、25.8、25.14 等） |
| `global_text/global_text.py` | `FDETXT00` 各條目的讀取端（`scan_readers`）、額外場景區塊哪些條目由哪支腳本顯示（`classify_scene_block`、`scene_refs`），以及 `src/` 裡 `fdps_draw_text` 條目運算式的解析（`_draw_calls`、`_resolve` 等）；`FDETXT00` 的分區（`REGIONS`、`MESSAGE_GROUPS`）與兩張文字頁的重建（`build_pages`） | `chapter_docs`、`cut_content`、`data_skill` |
| `data_tables/data_tables.py` | 名稱（`FDETXT00` 的條目基底）、`ENEMYDAT`／`RANKUP`／`SHOPnn` 與部署記錄的載入（`load` → `Game`）、使用效果代碼的說明（`USE_EFFECTS`），以及 `assets/` 產生表的閘門（`check`） | `chapter_docs`、`cut_content`、`cut_items`、`data_skill` |
| `cut_content/cut_content.py` | `cut_content/` 的頁面結構（`collect`、`exclusion_ids`、`TOPICS`）與遊戲檔的讀取（`read_game_file`） | `cut_content` 的各產生器、`cut_traces`、`chapter_docs`、`data_skill` |
| `cut_content/story.py` | 永遠不會顯示的文字與它歸哪個 `cut_content/` 條目（`never_shown_text`、`OWNERS`），以及把遊戲檔的容器解到暫存樹（`dump_tree`，給讀 `vfs_dump` 佈局的工具用） | `data_skill` |
| `game_mechanics/check_mechanics.py` | 知識庫頁面的共通規則：function 引用對快照、未知符號、相對連結（含 `cut_content/` 的待建連結）、禁引 `workspace/`／`legacy/`、流水帳字眼 | `chapter_docs` |
| `game_mechanics/apply_kb.py` | 把逐條判定的列插進 `rebuild_info/pitfalls.md` 的規則（`apply_pitfalls`） | `chapter_docs` |
| `chapter_docs/chapter_facts.py` | 每一章的機械事實（`facts`）：部署與波次、可搜尋格、事件 slot、本章的過場腳本、章節文字區塊每一條的讀取端（`text_readers`），以及章節頁的產生區塊（`render_block`） | `data_skill` |
| `chapter_docs/check_chapter.py`、`index.py` | 章節頁與 `chapters/_index.md` 產生區塊的填入與切分（`fill`、`regions`、`build_text`） | `data_skill` |

儲存慣例：腳本放 `tools/{工作名稱}/`，所有可重生的中間產物與輸出放 `workspace/{工作名稱}/`。知識庫不得引用 `workspace/` 下的路徑。例外是本身就要進版控的產物：Ghidra 文字快照寫到 `ghidra_snapshot/`，攻略站鏡像寫到 `docs/guide/`，遊戲資料查詢 skill 的資料集寫到 `.claude/skills/fdps-data/`，字模對照表、全域文字頁與額外場景文字頁寫到 `assets/text/`，`data_tables` 產生的表寫進 `assets/` 各正典檔裡標定的表格位置。刪減與未用的素材（PNG、SAF 內嵌音效的 WAV）寫到 `cut_content/media/`，CD 音軌轉成的 WAV 寫到不進版控的 `cut_content/media/cdda/`。人工輸入、無法重生的資料與讀它的腳本放在一起進版控：開發者在字模校對網頁填的字寫到 `tools/glyph/developer_answers.json`，章節頁的逐章判定（波次會不會部署、文字條目的讀取端）寫到 `tools/chapter_docs/judgements/`。

| 子資料夾 | 用途 |
| --- | --- |
| [`ail_link/`](ail_link/_index.md) | 把前作抽出的 `ailv3.lib` 連進客戶端程式並在 DOSBox-X 裡實跑，驗證音效初始化與播放（票 19） |
| [`backbone_walk/`](backbone_walk/_index.md) | 骨幹走查的全自動 workflow（票 12 專屬），也是 [ADR-0007](../docs/adr/0007-workflow-automation-and-agent-context.md) 五條原則的參考範例——是範例不是框架，別票自己寫自己的 |
| [`build_flags/`](build_flags/_index.md) | 反推建置旗標組：解 LE header、跨 Watcom 版本差分編譯、CRT 位元組比對、連結實驗 |
| [`build_gate/`](build_gate/_index.md) | 自我回歸閘：重建、與基準值做雜湊與重定位感知的比對、跑測試套件，回結構化判定（票 20） |
| [`call_graph/`](call_graph/_index.md) | 建出呼叫圖（含函式指標表的間接邊）並算可達性、孤島分量與共用 helper 排名 |
| [`cd_scope/`](cd_scope/_index.md) | 透過 DOSBox-X 把光碟映像的內容複製出來並清點 |
| [`cel_decode/`](cel_decode/_index.md) | 解出 `.CEL` 的每個 sprite 並算圖成 PNG |
| [`chapter_docs/`](chapter_docs/_index.md) | 30 份章節頁與 `chapters/_index.md`：從資料產生部署、寶物、事件、過場腳本與全文對白的區塊，逐章 agent 寫敘述與三種判定，閘門檢查結構、引用與判定，落地後可隨時重產比對（票 25.8 的 workflow） |
| [`code_emit/`](code_emit/_index.md) | 把 function emit 成 C 的整條流程：emit／review／gate／記帳的 workflow、單元測試映像的建置與執行、檔案落點的路由表、工作清單與進度（票 21、21.5），以及全部落地後的疑慮總掃（票 22） |
| [`crt_version/`](crt_version/_index.md) | 判定工具鏈的發行版：每個 `crt` function 對每個 Watcom 版本的程式庫逐 byte 比對，交集成單一版本，另以差分編譯量測編譯器（票 16 專屬的 workflow） |
| [`cut_content/`](cut_content/_index.md) | `cut_content/` 的共同入口：總表產生、結構閘門（條目、分類、編號、連結、素材命名與引用），以及各主題素材從原版遊戲檔重生與逐 byte 比對（票 25.10 建立，25.11–25.14 註冊各自的素材產生器） |
| [`cut_items/`](cut_items/_index.md) | 物品取得途徑的普查：照 `src/` 的每條給物品途徑窮舉整張物品表，產生 `cut_content/items.md` 的「拿不到的道具」表並逐格驗證（票 25.12） |
| [`cut_traces/`](cut_traces/_index.md) | 票 25.9 交下來的新痕跡逐條判定：一條一個 agent 驗證真假並決定落點（併入既有條目、新條目、排除、轉交別處或捨棄），gate、回掃、收尾報告進 `devlog/runs/`；落地由票自己轉錄（票 25.10、25.11 各有自己的 workflow，`--ticket` 選票） |
| [`cut_verify/`](cut_verify/_index.md) | 刪減與未用調查的 53 條發現逐條獨立驗證：一條一個 agent、一手證據的 gate、第二位 agent 回掃更正與分類有疑義者，收尾報告與判定彙整進 `devlog/runs/`（票 25.9 專屬的 workflow） |
| [`cutscene_script/`](cutscene_script/_index.md) | 把 `ICONANI.VFS` 的過場腳本解成步驟列表，標出每步當下的地圖、文字區塊與單位，並驗證全部腳本剛好走完（票 25.2） |
| [`data_emit/`](data_emit/_index.md) | 全域資料的定義落地：連結器的未定義清單當工作清單、每個全域一個判定、腳本轉錄進 `src/`、與原版映像逐 byte 比對的閘門（票 23） |
| [`data_skill/`](data_skill/_index.md) | 產生 `fdps-data` 查詢 skill 的兩份資料集（九張表、66 個文字區塊的每一條與它的讀取端或 `cut_content/` 歸屬），對知識庫的表逐列驗證並跑各頁產生器自己的閘門（票 25.16） |
| [`data_tables/`](data_tables/_index.md) | 從遊戲檔產生 `assets/` 的敵方全表、種族、職業使用者、法術總表、轉職路線、商店與使用效果代碼表，寫進知識庫並逐格驗證名稱與數值（票 25.7） |
| [`fdps_build/`](fdps_build/_index.md) | 在 DOSBox-X 內全自動編譯、連結並執行的建置流程，以及驗證這套工具鏈可用的最小程式 |
| [`game_build/`](game_build/_index.md) | 遊戲本體的建置（只編 `src/`，連結成 `FDE.EXE`）、原版與重建版並排遊玩的啟動器、當機位址到 function 的定位（票 24） |
| [`game_mechanics/`](game_mechanics/_index.md) | `program_info/` 遊戲機制頁的全自動 workflow：一頁一個 agent 起草、回掃、原版 bug 目錄、閘門通過才逐 byte 落地、踩雷點候選逐條判定後由腳本寫入共用頁（票 25.5 專屬的 workflow） |
| [`ghidra_baseline/`](ghidra_baseline/_index.md) | 複查 Ghidra 基準狀態：區塊屬性、孤立程式碼、未反組譯區域、error bookmark |
| [`global_text/`](global_text/_index.md) | 產生 `assets/text/` 的全域文字頁與額外場景文字頁：原文取自 `text_decode`、`FDETXT00` 的讀取端掃描 `src/`、場景條目由誰顯示取自 `cutscene_script` 的追蹤，閘門核對手寫分區與掃描結果（票 25.6） |
| [`global_data/`](global_data/_index.md) | 全域資料符號的語意命名與型別判定，以及主要 struct 的佈局定義與套用（票 17 專屬的 workflow） |
| [`ghidra_config/`](ghidra_config/_index.md) | Ghidra MCP 專案設定的正本：把命名檢查調成本專案的慣例 |
| [`ghidra_snapshot/`](ghidra_snapshot/_index.md) | 把 Ghidra 的分析狀態匯出成文字快照 |
| [`glyph/`](glyph/_index.md) | `FDETXT.FON` 字模對倚天字型逐像素比對、不吻合的字做成校對網頁讓開發者填字、合併成字模對照表（票 25.1） |
| [`guide_offsets/`](guide_offsets/_index.md) | 把攻略站給的資料表偏移對回 `MISC.VFS` 成員，解表並與攻略站數值逐筆比對 |
| [`guide_scrape/`](guide_scrape/_index.md) | 把攻略站的內容頁抓成原文鏡像並提供搜尋入口 |
| [`kbd_probe/`](kbd_probe/_index.md) | 量 DOS/4GW 之下手塞 BIOS 鍵盤環形緩衝區後，鍵盤查詢何時看得到那些鍵（測試端要先等一個 timer tick 的依據） |
| [`logic_naming/`](logic_naming/_index.md) | 遊戲邏輯 function 的語意命名、參數命名、calling convention 判定與行為註解（票 15 專屬的 workflow） |
| [`map_decode/`](map_decode/_index.md) | 地圖圖層（`DTL`／`MPL`／`ATTR`／`DSC`）與 `MAPnn.DAT`／`.COD` 的解碼、每一格的判讀、不變量檢查、每張地圖的統計表，以及整張地圖的 PNG 算圖（票 25.3；25.8、25.14 用它產生地圖全圖） |
| [`pool_rereview/`](pool_rereview/_index.md) | 每個 function 的 pool、名稱、邊界與 signature、plate comment 由第二雙眼睛重讀一次（票 14.2 專屬的 workflow） |
| [`pool_triage/`](pool_triage/_index.md) | 未辨識區塊逐一判定並建成 function，再逐一判定每個 function 的 pool 歸屬（票 14 專屬的 workflow，含 Watcom 執行期與 Miles AIL 的函式庫比對） |
| [`rle_asm/`](rle_asm/_index.md) | RLE 繪製的 15 支改回原版組語：逐道指令與原版比對、片段接成 `src/*.asm`、C 譯本與組語之間的切換（票 22.3） |
| [`saf_decode/`](saf_decode/_index.md) | 解出 `.SAF` 的四層結構、驗證自洽性，並算圖成 PNG 與 WAV |
| [`save_format/`](save_format/_index.md) | `FDE.SAV` 的讀寫：解密、驗檢查碼、把欄位解成 JSON，以及把改過的明文映像封裝回存檔 |
| [`text_decode/`](text_decode/_index.md) | 把 `FDETXTnn.TXT` 文字區塊解成逐筆可讀文字並標出控制碼（票 25.1） |
| [`vfs_dump/`](vfs_dump/_index.md) | 解開 `.VFS` 容器並驗證其自洽性 |
