# 2026-08-15 骨幹走查全自動：一口氣走完 100 個 function

票 12 的主段。前一篇（`2026-08-15-backbone-walk-batch01.md`）是手動盯著跑的 9 個 function，這次把整條流程接成一支無人介入的 workflow（`tools/backbone_walk/walk_ticket12.js`），從種子位址開始做廣度優先走查，走到預算用完為止。

結論先講：跑得很乾淨。三輪落地全部一次過，gate 每輪都是孤立程式碼 0、error bookmark 0，Ghidra 已存檔。真正值得記的不是成功的部分，是下面那幾件「跑完才看得出來」的事。

## 這次的形狀

五段：Walk（讀一個 function 寫一份判定檔）、Arbitrate（撞名時一個衝突一個 agent）、Apply（一輪的判定一次轉錄進 Ghidra 並跑稽核）、Tag（打 pool／subsystem 標籤）、Document（寫知識庫頁與這篇）。

撐住這個設計的兩件事跟 batch01 一樣，只是這次被 100 個 function 壓過一遍還站得住：

- 工作清單只活在腳本的迴圈裡，每次 `agent()` 只帶一個位址（ADR-0002）。
- reader agent 完全不寫 Ghidra，把完整判定——plate comment、prototype、證據、`open_question`、`walk_next`——寫進 `workspace/backbone_walk/verdicts/<addr>.json`，只回傳約 200 byte 摘要。所以走 100 個或走 500 個，workflow 腳本與後續每一段的 context 都不變大。寫知識庫那一段讀的是 `index.json` 這份精簡索引，不是 100 份 plate comment。

第二點是這次唯一真的被驗證的架構主張。三輪之後 `verdicts/` 有 109 個檔（含 batch01 的 9 個），沒有任何一段需要同時看超過一輪的量。

## 走到哪、為什麼停

三輪，10 → 38 → 52，共 100 個。**停下來的原因是 100 個的預算用完，不是走完。** frontier 還剩 77 個位址沒走，也就是說骨幹的邊界在這次跑完時仍然是開的。

100 個的信心分佈是 high 53、medium 46、low 1。子系統分佈：

| 子系統 | 個數 |
| --- | --- |
| graphics | 23 |
| battle | 21 |
| menu | 16 |
| file_io | 12 |
| cd | 9 |
| audio | 6 |
| memory | 3 |
| unknown | 3 |
| startup / input / main_loop | 各 2 |
| string | 1 |

pool 是 fdps 96、crt 3、ail 1。跟 batch01 幾乎全是 CRT 正好相反——那批走的是進入點到 `main` 的啟動鏈，這批一進 `main` 就整個掉進遊戲自己的程式碼裡。

## 三輪落地都乾淨，但有一件要修的

`no_return` 這次一個都沒有。batch01 那個「標了 no-return 就會把呼叫點後面的指令擠出 body 變成孤立程式碼」的坑因此完全沒被觸發——全 binary 目前四個 no-return function（`0x42e0f`、`0x43298`、`0x43310`、`0x4df4c`）都是 batch01 那批 CRT，這 100 個遊戲 function 沒有一個是。所以 gate 每輪都乾淨這件事，不能當成「那個坑修好了」的證據，它只是這次沒踩到。

唯一被 Ghidra 退件的是 `0x435a2` `crt_sprintf` 的 prototype：寫成 `int crt_sprintf(const char *...)` 會回 `Can't resolve datatype: const char *`。Apply 段當場改成

```
int crt_sprintf(char *buf, char *format, ...)
```

以 `__cdecl` 重送就過了——storage 與 varargs 完全相同，只掉了 `const` 限定詞。這是這個 program 的型別解析器的限制，不是這個 function 特有的，**任何之後帶 `const` 的簽章都會再撞一次**。跟 batch01 那個「`__watcall` 不能寫在 prototype 字串裡」是同一類的工具坑，已知的規避法都一樣：把資訊挪到別的地方表達，不要跟解析器硬碰。

## 唯一的 low confidence，以及它最刺的地方

`0x305a0`，最後叫 `fdps_unknown_000305a0`。它的 body 只有 12 條指令：Watcom 四推序幕、一個 `CALL 0x3d8b2`、收尾、`RET`。沒有引數、沒有字串、沒有常數、沒有全域參照。agent 的判斷完全正確——這個 function 自己身上沒有任何證據，它的身分等於 callee 的身分，所以照規則回 low 並在 `open_question` 明寫「要看 `FUN_0003d8b2` 才知道這是啟動、收尾還是模式切換」，沒有硬編一個用途填欄位。

刺的地方在下一輪：`0x3d8b2` 在第二輪被走到了，判定是 **`AIL_shutdown`**——Miles AIL 的公開 API wrapper，證據是 `0x623a5` 的字串 `"AIL_shutdown()\n"` 與整段 Miles log wrapper 的固定形狀。再對上 batch01 已經寫下的 `main` 收尾三連 `0x29440` / `0x305a0` / `0x3c4a7`（釋放全域配置、這個 wrapper、停 CD 音軌），`0x305a0` 是什麼幾乎已經寫在牆上了。

但 workflow 沒有回頭改的機制。判定檔是一次寫成的，reader 永遠只看自己那一個 function，後面幾輪長出來的知識不會回流到前面的判定；Apply 只是無損轉錄，不做判斷。結果就是 Ghidra 裡現在掛著一個 `fdps_unknown_000305a0`，而推翻它所需的證據已經躺在同一個資料夾裡的另一個檔案。

這跟 batch01 那次 `crt_common_init` 是同一個結構性缺口的兩面：那次是 orchestrator 手上有全部九份報告、人看到了就補上；這次自動化之後沒有人在那個位置，缺口就這樣留在成果裡。**這是這次跑完唯一欠著的東西**，要嘛給 workflow 加一段「開放問題被後續輪次回答」的回掃，要嘛就承認走查的最後一哩得有人看一遍 `open_question`。

## 仲裁段這次完全沒開工

100 份判定裡沒有任何兩個 function 撞名，三輪的 Apply 報告也都回報無重複。batch01 那 9 個裡就出過一次（`0x43298` 與 `0x43310` 都被提名叫 `crt_cstart`），當時看起來像是常態，現在看是特例——那兩個本來就是同一段組語被版權橫幅切成兩半，形狀高度相似才會撞。這批 100 個橫跨十二個子系統、彼此差異大，撞名的壓力自然低。

不能因此推論仲裁段是多餘的：走查繼續往深處走時，thunk／body、同族 wrapper 這種近似對會越來越多，撞名機率是往上的。這次只能記「跑了 100 個沒觸發」這個事實。

## 「反組譯是第一手」這條規則的實際收益

至少 8 份判定明確寫下 Ghidra 的 decompiled 參數列是錯的，而且錯法一致：`unaff_EBX`、憑空的 `param_1` / `param_2`、七個參數的簽章。根因在 `0x16200` 的判定裡講得最清楚——Watcom 的四推序幕（`PUSH EBX/ESI/EDI/EBP` 然後 `MOV EBP,ESP`）讓返回位址落在 `EBP+0x10`，真正的第一個引數從 `EBP+0x14` 起算；decompiler 沒對齊這個框架，就把引數往後數了三格，剩下的 `unaff_EBX` 全是猜出來的。

如果 agent 拿 decompiled C 當第一手，這 8 個的 prototype 會整批錯。這條規則是寫死在 prompt 裡的，這次算是拿到了它的收益證據。

順帶一個對票 11 的旁證：這 100 個裡 fdps pool 有 82 個判成堆疊慣例（Ghidra 記成 `__cdecl`），15 個 `__watcall`，而那 15 個裡有 14 個 prototype 是 `void f(void)`——沒有引數時慣例本來就分辨不出來，那是套用預設而不是量到 register 慣例。換句話說，**走查沒有找到任何一個真的用 register 傳引數的遊戲 function**，與 `rebuild_info/build_flags.md` 判定的預設 `-4s` 一致。

## 開放問題是常態，不是失敗

109 份判定裡有 105 份帶 `open_question`。這不是品質問題，是「只准讀這一個 function」這個限制的必然邊界。兩個代表性的例子：

- `0x10010` `fdps_map_actor_behavior_step`：分不出它走的是戰鬥格盤還是城鎮／大地圖。`Chess.wav` 與地形傷害格支持戰鬥，但 mode 5 的事件槽機制（`DAT_000640d8` 旗標、`DAT_0006013c+0x53` 的 3 byte 表）同樣可以是腳本事件。
- `0x11460` `fdps_move_path_trace`：方向碼是從起點往終點沿著成本圖走下坡記錄再反轉的，但成本圖是從單位那格還是目標那格灌的，只看這個 function 看不出來，所以哪一端是單位沒定案。

這兩個都要等 caller 被走到才會收斂，而它們的 caller 就在 frontier 上。這批問題應該由後續輪次自然吃掉，而不是現在硬猜——但這又回到上面那個回掃缺口：吃掉之後沒人會回來改判定檔。

## 剩下的 frontier

77 個位址，幾個看得出形狀的群：

- **13 個 blit 模式**：`0x56a0d`、`0x56a8d`、`0x56b25`、`0x56bb7`、`0x56c5e`、`0x56e2a`、`0x57114`、`0x57551`、`0x575ed`、`0x5761b`、`0x57793`、`0x57916`、`0x57a74`——正好對上 `0x568db` `fdps_blit_dispatch` 依 mode byte 分派的十三個 pixel-copy routine。整個 graphics 底層的實作都在這裡，形狀規律。
- **4 個 CD request builder**：`0x3c34f`、`0x3c0c8`、`0x3c803`、`0x3c452`，被已走完的 CD 那 9 個共用。走完這批，CD 子系統大致就封閉了。
- **戰鬥移動群**：`0x120d0`、`0x12230`、`0x126b0`、`0x127c0`、`0x12c10`、`0x12e50`、`0x13040`、`0x13420`、`0x13c90`、`0x13e10` 等，由 `fdps_map_actor_behavior_step` 的 mode 分派表請求，上面那個「戰鬥格盤還是大地圖」的問題就掛在這群上。
- `0x13ed0`：per-cell 的 tile drawer，擁有那個 0x24 byte 繪製 context 的其餘欄位（blend mode、blend level、clip target）的定義，被 graphics 那邊點名兩次。

請求來源以 battle 最多，graphics 次之。下一輪如果只能挑一群，`0x568db` 底下那 13 個的性價比最高：位址連續、形狀同構、而且它們是所有繪圖路徑的共同終點。

## 一個噪音源

第 1 輪與第 3 輪的 Apply 報告都提到 `C:\Users\fdpsf\ghidra_scripts` 底下留著 8 個舊的 `McpInline_*.java` / `T.java`，是之前 inline script 呼叫留下的殘骸。它們編不過，於是每次 `run_ghidra_script` 之前都會先吐一段 bundle activation 的 stack trace。`ApplyBackboneWalk.java` 與 `AuditGhidraBaseline.java` 兩支都照常跑完，結果沒受影響，但輸出被塞得很難讀——判斷 gate 有沒有真的過會多花一輪眼睛。清掉即可，沒有別的影響。

## 後記：那個欠著的東西當場補掉了

上面說「Ghidra 裡現在掛著一個 `fdps_unknown_000305a0`，而推翻它所需的證據已經躺在同一個資料夾裡的另一個檔案」——收尾複查時就照這個線索補了。

親自讀了 `0x305a0` 的反組譯確認 body 真的只有 Watcom 四推序幕、`CALL 0x3d8b2`、收尾、`RET`，再確認 `0x3d8b2` 的判定是 high 信心的 `AIL_shutdown`（證據是 `0x623a5` 的 `"AIL_shutdown()
"`），而且 `0x305a0` 與 `0x304e0` `fdps_audio_init` 在 `main` 裡剛好是對稱的一啟一關。三條合起來足夠定案，改名為 `fdps_audio_shutdown`，信心 high，判定檔裡用 `_supersedes` 欄位留下「第一次讀為什麼只能給 low」的記錄。

所以走查最後是 109 個全部有語意名稱、0 個 low confidence。但這不代表缺口補好了——**補它的是人，不是 workflow**。結構性的問題原封不動：reader 只看自己那一個 function，後面輪次長出來的知識沒有任何機制回流。這次剛好只有一個，下次走更深、wrapper 更多的時候不會這麼便宜。真正的修法是給 workflow 加一段回掃：走查結束後，把所有 `open_question` 非空或信心 low 的判定挑出來，用當時已經齊全的判定檔重讀一次。這件事沒做，記在這裡。
