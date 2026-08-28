# 2026-08-28 票 22 開跑前：把票 21 的流程放大到 514 支要先補的東西

票 22 說得很清楚：沿用票 21 的 workflow，不另外設計，規模放大。實際動手才發現「放大」不是把清單變長就好，有三件事在一支 function 的規模下完全不會出現，在五百支的規模下每一支都會撞到。這篇記的是這三件事怎麼撞出來的，以及一個差點做錯的決定。

## 一、先確認上一輪的成果還站得住

開工第一件事是跑一次現況：`build_emit.py all` 綠、13 個 check 全過。這是票 21 留下的 `fdps_menu_find_first_enabled_entry`，也是唯一一支 emit 完的 function。基準確認過才敢動建置腳本。

## 二、順序：差點就照位址發清單

`next_batch.py` 原本 `sorted(functions.items())` 發清單，也就是照位址。票 21 只有一支 function，看不出問題。

問題是這樣的：假設先 emit 了 `fdps_map_actor_behavior_step`（位址 `0x10010`，全程式最小），它呼叫的十幾支 callee 一支都還不存在。連結不起來，除非給 callee 補 stub；補了 stub，替它寫的測試量到的就是 stub 回傳的 0，而不是真的 callee。這種測試今天會過，等 callee 真的 emit 完那天變紅——最糟的發現時機。

所以改成 callee 先於 caller。做法是把 call graph 縮到 routing 名冊上的 514 支，Tarjan 縮環，再對縮圖做拓樸排序（`emit_order.py`）。

排完的數字比預期好很多：**只有一個環，10 支 function；caller 早於 callee 的配對只剩 11 對**。也就是說五百多支裡有五百支可以在 callee 全部是真貨的情況下 emit。那個環是 `fdps_map_actor_behavior_step` → `take_best_action` → `move_and_attack` → `cast_chosen_spell` → … → 第 8 章的兩支事件 handler 繞回來，AI 行為分派與章節腳本互相呼叫，本來就沒有 callee-first 的排法。

**踩到的坑：graph.json 是舊的。** 它上次產生是 8/15，那時程式裡有 1042 支 function；現在是 1345 支（票 14 之後建了不少）。照舊圖排出來的順序會漏掉新 function 的邊。重跑 `BuildCallGraph.java` 之後 direct_edges 從 3167 變 3741。這件事沒有任何機制會提醒，是排序結果看起來「環太少」時回頭查才發現的——結論是 `emit_order.py` 要跑之前一定要先重建 graph。

Tarjan 的實作特意寫成迭代版而不是遞迴版：514 個節點的深度優先搜尋在 Python 的預設遞迴上限下會爆。另外雖然 Tarjan 本身吐出來的分量順序已經就是反拓樸序（也就是 callee-first），還是另外做了一次 Kahn 掃描重新導出——不是不信任它，是不想讓「順序正確」這件事依賴一個沒有寫下來的性質，將來誰換掉 SCC 實作就會無聲地把整份工作清單打亂。

## 三、連結：票 23 還沒開始，但每一輪都要連得起來

票 22 的 gate 是 build gate，必須連得起來；但全域資料的真值是票 23 的產物。票裡指定的做法是兩段式連結，實作起來有幾個決定：

**stub 從哪來。** 第一次連結不帶 stub，把連結器報的未定義符號抓下來，對 `routing.json` 查型別與大小，產生 `STUBS.C`，第二次連結帶上它。這份未定義清單同時就是票 23 的工作清單，每次建置重新產生，所以不可能跟程式碼不一致。

**為什麼不乾脆一次生成全部 232 個全域的 stub。** 那樣只要一次連結，省一次 DOSBox 啟動。沒這樣做是因為票 22 要的那份清單——「已 emit 的程式碼真的要什麼」——只有第一次連結報得出來；從「票 17 全表減去 `src/` 已定義」算出來的是上界，不是實際需求。多花的那次連結只編一個檔（物件檔都還在，第二趟只 `WCC386 STUBS.C` 加 `WLINK`），實測 2 秒。

**型別要不要照抄。** 一度想全部用 `unsigned char name[size]` 湊 byte 數，簡單得多。沒這樣做的理由只有一個：對齊。程式碼把 `data_fdps_palette_shade_ramp_table` 當 `unsigned int[]` 讀，char 陣列在 Watcom 下不保證 4-byte 對齊。所以還是照票 17 的型別宣告，型別對不上大小時（`elem * count != size`）才退回 byte 陣列，並在註解裡說明是退回來的。

**三類符號拒絕 stub。** routing 不認得的名字、routing 標記為不 emit 的符號（字串字面值、區域陣列初值、switch 表）、以及 `src/` 已經定義過的。第一類特別重要：拼錯的全域名如果被照樣 stub 成零，那就是把一個連結錯誤變成一個看起來很合理的答案。

**「已經定義過」的判斷寫錯過一次。** 第一版拿正則掃 `src/*.c` 的行首，結果把 `extern unsigned char data_fdps_audio_bgm_enabled_flag;` 也算成定義——而那正是每一支 emit 出來的 function 都會寫的東西。用一個臨時的探針檔測兩段式連結時當場撞上：四個符號有三個被判成「`src/` 已經定義了」，建置直接失敗。修法是先把 `extern ...;` 整段從文字裡拿掉再掃，並補了兩條 selftest 釘住「extern 不是定義」與「定義找得到」。

順帶把 `ailv3.lib` 與那七條 alias 加進 emittest 的連結指令列。票 22 遲早會 emit 到 `audio.c`，那時 18 個 `AIL_*` 進入點就要解得掉；alias 清單直接 import `link_ail.py` 的 `ALIASES`，沒有抄第二份——名稱對應表抄兩份就是其中一份會過期。

## 四、struct：本來想讓每個 emitter 自己抄

原本的想法是 emitter 需要哪個 struct 就從 Ghidra 抄哪個進 `fdpstype.h`。想了一下否決了：514 支 function 就是 514 次抄錯欄位偏移或號性的機會，而且抄進去之後下一個人會信它。更重要的是 struct 佈局根本不是逐 function 的判定——票 17 已經整體定案過了。

所以寫 `gen_types.py` 從 `ghidra_snapshot/data_types.txt` 產生 `src/fdpstype.h`，23 個 `fdps_*` struct，337 行。兩件事非做不可：

- **洞要填。** Ghidra 只列有名字的欄位，`fdps_unit_record` 的 `+0x022` 之後直接跳到 `+0x031`，中間 15 個 byte 沒人命名。漏掉的話後面每個欄位都會往前移。全部補成 `gap_022[15]` 這種明寫偏移的填充陣列。
- **packing 要自己講。** wcc386 的預設本來就是 `-zp1`，但這些偏移是原版的，不能靠一個旗標留在原地。標頭自己寫 `#pragma pack(1)`。

順手多產一個 `tests/fdpstype.c`：每個 struct 的 `sizeof` 加每個欄位的 `offsetof`，期望值取自快照而不是標頭——拿標頭當期望值只會證明標頭等於它自己。252 個 check 一次過，等於實測確認了「wcc386 預設對齊 = 原版佈局」這件之前只在最小驗證程式上量過的事。

## 五、workflow 本身改了什麼

`emit_ticket22.js` 是從 `emit_ticket21.js` 複製過來改的（票 22 明說沿用，ADR-0007 也允許照抄小工具）。三處改動：

1. **工作清單用問的，不用帶的。** 票 21 把 `functions` 陣列塞在 `args` 裡；514 支的話那是一百多 KB 穿過 orchestrator。改成 workflow 開一個 agent 跑 `next_batch.py`，把清單拿回來。副作用是每次呼叫都自帶續跑能力，不必記得上次跑到哪。
2. **拆檔段。** routing 的預估最大檔是 `btlturn.c` 947 行，實際 emit 出來超過 1000 行是很可能的事。超標而且後面還有 function 排在該檔時，插一段拆檔 agent，照 `code_layout.md` 的規則改 `build_routing.py` 重產路由、搬程式碼與測試、更新知識庫、跑 gate、單獨 commit。拆完這一輪就結束——手上那份工作清單已經過期了，下一輪重新問。
3. **stub 的存在要講給 emitter 與 reviewer 聽。** 清單裡每一項都帶 `stubbed_callees`，reviewer 的檢查表加了兩條：宣告的擁有者對不對，以及有沒有測試在斷言 stub 的回傳值。

## 尚未驗證的

拆檔那一段還沒有真的觸發過——要等某個檔真的長到 1000 行以上才會跑到。寫在這裡當提醒：它是本次唯一一段沒有被執行過的程式碼。
