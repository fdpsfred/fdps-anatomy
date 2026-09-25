# game_mechanics — 遊戲機制頁（票 25.5）

產出 `program_info/` 裡回答「這個遊戲怎麼運作」的頁面：戰鬥數值與回合、敵方 AI、移動範圍、法術與道具效果、章節生命週期與事件分派、過場腳本直譯器、對話系統、村莊、存讀檔流程，以及已知原版 bug 目錄。一個子系統一頁，每條公式與規則都標出實作它的 function。

中間產物在 `workspace/game_mechanics/`：`drafts/<doc>.md` 與 `drafts/<doc>.meta.json`（每頁的草稿與給其他階段讀的中繼資料）、`pitfalls/<id>.json`（每個踩雷點候選或 bug 交叉連結的判定）、`index_rows.json`（`program_info/_index.md` 的新列）。

| 檔案 | 用途 |
| --- | --- |
| `mechanics_ticket25_5.js` | 全自動 workflow。清單只存在於腳本內，每個判定 agent 只拿到一頁或一個候選，只寫自己的判定檔。階段：起草 → 回掃（留有未決問題或低信心的頁再讀一次，允許讀其他頁的草稿當證據；先備份，改壞就還原）→ `known_bugs.md`（彙整各頁回報的原版 bug 與其他票交來的線索，逐條親自複核，也有回掃）→ 以中繼資料的 `complete` 為準決定哪些頁可落地、`land.py` 落地、落地後的閘門 → 踩雷點候選與 bug 交叉連結逐一判定（依序，後面的判定可讀前面的判定檔避免重複）→ 提出索引列 → `apply_kb.py` 落地兩個共用頁並跑連結閘門 → run record 與 devlog。錯誤處理：每頁重試一次；一輪全無回傳或依序的判定連續兩個無回傳即視為上游失效而停止；起草者回報 Ghidra 失去回應也停止；停止後不跑任何寫知識庫的階段，所有略過的項目列進未完成清單，run record 照寫。每個判定 agent 開工先檢查自己的判定檔，兩支落地腳本都是冪等的，所以中斷後整支重跑即是續跑。`args`：`{"date": "YYYY-MM-DD", "only": [頁名, ...]}`，`only` 可省略 |
| `check_mechanics.py` | 閘門。`--draft DOC...` 檢查草稿（連到仍是草稿的兄弟頁算通過）、`--landed DOC...` 檢查已落地的頁、`--links FILE...` 只檢查其他共用頁的連結。錯誤：開頭缺「驗證對象」、`` `name`（`0xaddr`） `` 與 Ghidra 快照不符、反引號裡的 `fdps_` 符號在快照與 `src/` 都找不到、某個 `##` 段沒有任何 function 引用、相對連結斷掉、猜測 `cut_content/` 內部的路徑、引用 `workspace/`／`legacy/`、流水帳用語（日期、票號、「後來發現」之類）。警告：連到還不存在的 `cut_content/_index.md`（該資料夾由刪減與未用的工作建立）。快照名稱表 import 自 `data_emit/check_data.py`。`--selftest` 跑內建測試 |
| `land.py` | 頁面的轉錄。草稿通過閘門才逐 byte 複製到 `program_info/`，不通過就拒絕並列出原因，不做任何修改。`--selftest` 跑內建測試 |
| `apply_kb.py` | 共用頁的轉錄。`pitfalls` 先把判定檔裡決定新增的列插到 `rebuild_info/pitfalls.md` 對應節的表尾，再把決定加連結的列逐字替換（連結可以指向同一批新增的列；該列在判定之後被別人改過就拒絕，不猜）；新增的列以原文或「同批連結改寫後的樣子」出現都算已存在，所以重跑不會重複插列；`index` 把 `index_rows.json` 的列加進 `program_info/_index.md`，已有該頁的列就不動。兩者都保留原檔的換行字元、重跑不會重複加列。`--selftest` 跑內建測試 |
| `collect.py` | 從各頁的 `.meta.json` 產生精簡索引，形狀即 workflow 要的結構：各頁的完成度與未決問題數、踩雷點候選（依 id 合併）、原版 bug、被封住的內容候選、讀不了的檔。`--selftest` 跑內建測試 |

閘門看不見的是公式本身對不對，以及是否**每一條**規則（而不只是每一段）都標了 function——那是起草 agent 對照組語的判斷，以及全知識庫逐條驗證的工作。續跑偵測不到草稿寫完之後 `src/` 又被改過的情形；本 workflow 執行期間 `src/` 不預期變動。

引用 function 的寫法固定為 `` `fdps_name`（`0x1ecc7`） ``，位址是 function 的**進入點**；要指 function 內部的某條指令時寫成「`` `fdps_name` `` 內的 `` `0x1ed02` ``」。
