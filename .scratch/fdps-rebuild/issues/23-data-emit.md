# 23 — Data emit

**What to build:** 全域資料的真實內容還原成 C 的初始值，讓程式碼能真正連結起來。工作清單以連結器回報的未定義符號為權威來源——這保證不會遺漏。

**執行方式：** 依 [ADR-0007](../../../docs/adr/0007-workflow-automation-and-agent-context.md)，由 workflow 全自動跑完，每次 agent 呼叫只帶**一個資料符號**。agent 把還原出來的初始值與判定依據寫成檔案、只回傳摘要；寫進 `src/` 是獨立的轉錄階段；gate 是 build gate。

本票自己寫 workflow，不改造票 22 那支（ADR-0007：共用的是原則，不是程式碼）。但**票 21.6 定下的無人值守與中斷復原是照著做的**，正典在 [`rebuild_info/emit_pipeline.md`](../../../rebuild_info/emit_pipeline.md) 的「中斷復原」與「批次大小是呼叫者的決定」兩節：

- **開跑第一段先收拾**，不是失敗時才收拾。usage limit 是把 session 就地砍斷，失敗路徑上的清理 agent 不會有機會執行。
- **能碰的路徑由界線決定**，界線外的髒狀態停下來報告而不是自行處理；反過來，本票 workflow 自己也只准弄髒它自己的產出路徑，界線外的改動當場單獨 commit，收尾寫的 devlog 也要自己 commit。
- **workflow 不管自己的預算**，跑完呼叫者給的數量為止；停下來的理由只有三種：清單跑完、上游失效、外力中斷。
- **收尾報告分得出「還沒輪到」與「跑到一半被中斷」。**

本票的工作清單有一個別票沒有的性質：**它是連結器產生的，而且會隨工作進展縮短**。所以迴圈的形狀是「跑連結 → 取未定義符號清單 → 一個符號一個 agent → 落地 → 再跑連結」，直到清單為空。這比固定清單更可靠，因為完成判定就是清單自己空掉，不靠計數。

## 沒有狀態檔，所以「足跡」要另外決定怎麼做

上一段那個性質跟票 21.6 的機制有一個真實的落差，動手前必須先解決，不能默默跳過。

票 21.6 的做法是每支開跑前在 `emit_state.json` 寫一筆 `in_flight`（刻意不 commit），讓被砍斷的 session 留下一行「死在這一支」的足跡。本票沒有那個檔——清單每次由連結器重生，這是本票刻意的設計。拆開來看：

- **殘骸清理沒問題。** 它靠路徑，不靠狀態：`src/`／`tests/` 的未 commit 改動就是殘骸，丟掉。
- **重新排隊也沒問題。** 被中斷的符號沒有落地，下一次連結它自然又出現在未定義清單裡。
- **足跡沒有地方寫。** 於是收尾報告分不出「還沒輪到」與「跑到一半被中斷」，而那是上面列的驗收條件之一。

兩條路擇一，並在票裡寫明理由：加一個輕量的 in-flight 標記檔（一個符號一行，不 commit），或者論證本票不需要足跡並改寫該條驗收條件。**不可以留著不決定。**

順帶一件要一起決定的事：票 21.6 的界線是一組明確的路徑，其中 `tools/code_emit/data/` 是票 22 狀態檔的位置。本票若把自己的標記檔或判定索引放到別處，界線就得同步擴，否則它自己的產物會被自己的收拾段判成界線外而停機。

回掃段在這裡對應的是：型別判定為低信心的符號，在相鄰符號都還原完之後重讀一次——資料表的欄位語意常常要看過相鄰表才明朗。

## 票 22 交接過來的：資料定義必須帶上的性質

票 22 的疑慮總掃留下 12 則只有本票答得了的疑慮（`python tools/code_emit/sweep.py handoff` 印出每則全文，`emit_issues.json` 裡 `status` 為 `handoff`、`handoff_to` 為 `23`）。共同點是：**零填充的 stub 或 routing 的預設型別會讓重建版行為與原版分岔**，本票定義這些符號時要照下表做，並在連結結果（link map 或 `WDISASM`）裡確認。

| 疑慮 | 符號 | 本票必須做到的 |
| --- | --- | --- |
| `00015be0#0` | `data_fdps_ui_palette_cycle_phase` | 初值 15（原版 `0x60014` 是 `0f 00 00 00`），不是零 |
| `00015be0#1` | `data_fdps_timer_tick_counter` | 定義帶 `volatile unsigned int`，與 `gamedata.h` 一致；`gamedata.c` 要 include `gamedata.h` |
| `0001f510#0`／`#1` | 浮動指示佇列四個符號 | `cell_x_offset[200]`、`unit_idx[200]`、`glyph_ids[200]`、`count` 依序緊鄰無填充（原版 `0x64120`／`0x641e8`／`0x642b0`／`0x64378`）；游標會越界寫進下一張表，佈局就是行為。`0x6437c` 之後原版擺的東西也要跟著擺 |
| `0001c520#0`／`#1`、`00019f80#0`、`0002dcf0#0` | 地形修正表 ap／def 與 `data_fdps_village_mode_flag` | ap `int[6]`、def `int[6]`、village flag（零填充 dword 的低 byte）依序緊鄰，def 在 ap 之後 `0x18`、flag 在 def 之後 `0x18`；索引 6 讀過表尾靠的就是這個相鄰 |
| `00030740#0` | `data_fdps_audio_sample_handle_table` | 元素 0 前面 4 byte 必須是重建版自己擁有、初值為零、沒人寫的儲存（例如同一物件裡前置一個零 dword）；`-1` 索引會讀到它。不能靠獨立 tentative 定義的相鄰。沒做到會讓無音效時的升級視窗卡死 |
| `0003bade#1` | `data_fdps_cd_int_regs_in`、`data_fdps_cdrom_int_out_regs`、`data_fdps_cd_int_sregs` | 型別用 `cd.h` 宣告的 `union REGS`／`struct SREGS`，不是 routing 的 `unsigned char[]`，檔案要 include `cd.h` |
| `000567a0#2` | `data_fdps_input_scancode_queue_write_index` | 定義帶 `volatile int`，與 `keybd.h` 一致 |
| `0002dcf0#1` | `data_fdps_ui_terrain_hud_panel_offset` | `short`，初值 `0x19` |

另外一則不在疑慮清單裡、同樣歸本票：`routing.json` 把 `data_fdps_battle_ai_best_physical_target_x` 記成 `/uint`，`src/gamedata.h` 宣告的是 `extern int`。定義時以號性判定為準（看讀取端的比較指令），兩邊對齊；不一致會直接編譯錯誤，不會靜默跑錯。

**Blocked by:** 21.5, 21.6, 22

**Status:** ready-for-agent

- [ ] 以連結器的未定義符號清單作為工作清單，迴圈跑到清單為空
- [ ] 每個資料符號逐一判定用途與型別，不用批次規則套用
- [ ] 全程無人介入跑完，agent 的 context 用量不隨符號數成長
- [ ] workflow 有錯誤處理：agent 未回傳或判定檔缺漏會重試、落地與 gate 失敗會明確回報、上游工具失去回應有停止訊號；收尾報告列出完成數、失敗數與未完成清單
- [ ] 開跑第一段先收拾上一輪中斷的殘骸，界線明確，界線外的髒狀態停下來報告
- [ ] 「足跡」的問題已明確決定（加標記檔或論證不需要），不是默默跳過；界線的路徑集合含本票自己的全部產出路徑
- [ ] workflow 不管自己的預算，跑完呼叫者給的數量為止
- [ ] 收尾報告分得出「還沒輪到」與「跑到一半被中斷」
- [ ] 每一批結束時 `git status --porcelain` 是空的，收尾寫的 devlog 是它自己的 commit
- [ ] 需要真實內容的資料符號從 binary 還原成 C 初始值
- [ ] 資料表內容與攻略基準真值交叉比對
- [ ] 連結器不再回報未定義符號
- [ ] 每個資料符號落在票 21.5 routing 指定的檔案
- [ ] 資料符號的分類與判定依據進知識庫
- [ ] 「票 22 交接過來的」那張表逐項做到並在連結結果裡確認
