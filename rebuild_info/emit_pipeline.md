# Emit pipeline — 一個 function 怎麼變成 C

**驗證對象**：把 Ghidra 的分析結果變成 `src/` 裡一支通過驗證的 C function 的整條流程。角色分工、emit 時必須在程式碼裡表達什麼、測試怎麼組織、工作狀態放在哪，以此檔為唯一正典。

旗標組由 [`build_flags.md`](build_flags.md) 擁有，閘門的判定規則由 [`build_gate.md`](build_gate.md) 擁有，符號命名由 [`naming.md`](naming.md) 擁有，本檔只引用。腳本在 [`tools/code_emit/`](../tools/code_emit/_index.md)。

## 四個角色，一次一個 function

| 角色 | 做什麼 | 不做什麼 |
| --- | --- | --- |
| Emitter | 讀三源（plate comment、disassembly、decompiled C），寫 `src/` 的 C 與 `tests/` 的測試，自己跑一次建置 | 不寫 Ghidra；不碰別的 function |
| Reviewer | 從 assembly 獨立驗證，並讀本輪的工作區 diff | 不信 emitter、不信 decompiled C；不寫 Ghidra、不改程式碼 |
| Gate | 跑 build gate 的 `emittest` 目標 | 不修任何東西——修了下一輪的 emitter 就不知道壞在哪 |
| Bookkeeper | 套用兩份判定檔裡提出的 Ghidra 修正、更新工作狀態、把兩份判定檔的 `open_issues` 併進 `emit_issues.json`、commit | 不下任何判斷 |

回掃不是每批的角色，是整件工作結束後的一次總掃，見下節。

**併行度是 1，而且這是設計不是限制。** Reviewer 判斷「本輪改了什麼」的依據是工作區相對 `HEAD` 的 diff，所以同時只能有一個 function 在飛；兩個的話彼此的改動會出現在對方的審查範圍裡。要提高併行度必須先解決 reviewer 的檢視範圍問題（例如 worktree 隔離），那是獨立的決定。

**Reviewer 的視野要含新增檔。** 一個模組的第一支 function 會把 `.c`、`.h`、測試三個檔全部新建出來，而 `git diff` 看不到未追蹤的檔——不先做一次 intent-to-add（`git add -N -- src tests`），reviewer 對「本輪改了什麼」的視野正好在最需要看的那份程式碼上是空的。

**Gate 紅燈要繞回 reviewer，不是只繞回 gate。** 建置閘門失敗多半代表 emit 出來的 C 是錯的，那正是最需要第二次審查的時候；修完直接重跑 gate 就 commit，等於用「編得過」取代「審查過」，而 commit 標題寫的是後者。

**Ghidra 的寫入集中在 Bookkeeper。** Emitter 與 reviewer 對 Ghidra 唯讀，發現描述錯誤時把修正寫進自己的判定檔，由 bookkeeper 在審查通過後一次套用，套用後跑 Ghidra 側的閘門並重新匯出文字快照。判定與落地分離的理由見 [ADR-0007](../docs/adr/0007-workflow-automation-and-agent-context.md)。

**判定寫檔，只回傳摘要。** 完整的判定寫進每個位址各一份的 emit 與 review 判定檔（落點見 [`tools/code_emit/`](../tools/code_emit/_index.md)），回傳給 workflow 的只有約 200 byte。「這一項做完了沒」由讀得到檔案的下一個角色回報，不採信寫檔者自己的宣稱。

## 疑慮在 emit 當下收斂，剩下的等總掃

**記一則疑慮的條件是「答案所需的證據還不存在」，不是「我還沒去查」。** 下面這些永遠拿得到，動手前要先用掉：Ghidra 唯讀查詢（`get_xrefs_to` 掃遍每個呼叫點、任何 caller／callee 的 disassembly、全域的 `search_instructions`）、出貨的遊戲檔與資源、知識庫、**以及把剛寫好的那支 function 用原版旗標重編出的目的檔**（`tools/build_flags/fn_match.py` 與 `FDPS.LE` 逐 byte 對照）。最後一項最常被忘記：「編譯器是不是真的那樣做」不必用猜的。本輪建置自己產出的 `.OBJ` 用的是 `-s -ot -od` 而不是原版旗標，不能直接拿來與原版比（[`build_flags.md`](build_flags.md)）。

理由是成本落點。掃描本身在哪一段做都一樣貴，省不掉；能省的是**重新建立上下文**——emitter 手上已經有三源、有剛建置出來的目的檔，換一個 agent 事後來問，得把這些從零讀回來一次。

真的無解才記：callee 還沒 emit、資料符號還在連結器的未定義清單上（它的定義由 [`data_emit.md`](data_emit.md) 的 pipeline 落地）、只有實機跑得出來的行為。前兩項現在都是空集合——全部 function 與全域都有定義——所以能記的只剩實機行為。

**判定檔記錄發現，不立法。** 不要在 `needs` 裡寫條件式義務（「若證實 X 就應改寫成 Y」）。下游會把它當指令執行，而中間沒有任何一關檢查那條規則本身站不站得住——例如「若這是 inline 展開，此處就該改成真的呼叫」，在不帶 `-oe` 的建置下照做會放進一條原版沒有的 `CALL`。

標準是 [ADR-0001](../docs/adr/0001-only-functional-equivalence.md) 的功能等價，明確不含暫存器配置、指令選擇這一層，**也不含還原原始碼的字面**。兩種拼法只要行為等價就都對，偏好其中一種是註記，永遠不是 blocking。

## 回掃是一次總掃，不是每批一次

[ADR-0007](../docs/adr/0007-workflow-automation-and-agent-context.md) 第四條要求的是「**工作結束前**」重讀所有未決判定。一批不是一項工作：全部 function 落地之後才做一次總掃，逐條處理 `emit_issues.json`。

每批做一次的回掃不成立。問題不是空轉，而是三件事：七成的結論用的是 emit 當下就拿得到的證據，該在當下收斂；只有資料定義或實機驗證答得了的疑慮每批被重問一次而資訊量完全沒變；還有一次一支的形狀看不見共同根因，而未決疑慮明顯成族（同一個索引上界問題、同一個 inline 展開問題），總掃可以一次結掉一族。

總掃的形狀：一次一則不批次、判定寫檔、先按根因分群再派工、需要改 code 的裁決交給獨立的落地階段、仍然無解且答案只有資料定義或實機驗證拿得到的列成交接清單不混進「已處理」。

**總掃的範圍不是只有 `status: open`。** 已經標成 `resolved` 的條目也要複查標籤是否成立——結論本身不重新推導，但「它是不是真的 blocking」要對過 ADR-0001。照判定檔裡的條件式義務標上的 BLOCKING 可以帶著 `resolved` 狀態存在（實例：`0002af60` 的 inline 展開疑慮）；只篩 `open` 的總掃永遠不會看到它。

**總掃之後疑慮有三種終局，不是兩種。** `resolved` 是答出來了；`handoff` 是答案需要的證據只有後續工作拿得到——資料 emit 決定重建版怎麼定義某個資料符號（[`data_emit.md`](data_emit.md)），或實機驗證看到某個畫面（[`playtest.md`](playtest.md)）——而且每則寫明接手的一方要看什麼、要決定什麼；`open` 是證據哪裡都不存在。三者分開記，交接清單才不會混進「已處理」。原版映像的資料初值在 Ghidra 裡隨時讀得到，所以「這張表裡是什麼」從來不是交給資料 emit 的理由，資料 emit 回答的是重建版要怎麼定義它。

指向資料 emit 的交接項都拿得到答案：每個全域的定義、型別與佈局約束都已落地，正典是 [`data_emit.md`](data_emit.md) 與 `tools/data_emit/data/manifest.json`。仍待後續的只剩實機驗證那一類。

**輸入全靠 `emit_issues.json`，所以每一則都要有 `status`（`open`／`resolved`／`handoff`）與 `from`（`emit`／`review`）。** 缺欄位的條目會從此後每一次「還有哪些未決」的篩選裡靜默消失，而 bookkeeper 是唯一寫它的人。**一個結論只記一次**：emitter 與 reviewer 記到同一件事時，settle emitter 那則，reviewer 那則用 `same_as` 指過去——兩份逐字複本的意思是將來發現其中一份錯了，只會改到一份，留下另一份繼續矛盾。

「只記一次」約束的是**描述**，不是**查證**。reviewer 仍然要自己把疑慮重推一遍，那正是獨立 review 的意義（實例：`0001f510` 的 reviewer 自己掃了全 image 對該全域的 24 條參照，才確認游標真的沒有上界）；它只是不再重寫一次描述，`what` 裡只留自己這趟多出來的東西——確認了什麼、怎麼確認的、判讀哪裡不同、emitter 漏掉哪個位址；什麼都沒多出來就寫一行講明。

## 順序是 callee 先於 caller

工作清單不照位址排，照 call graph 的拓樸序排：一支 function 的 callee 全部先 emit 完，才輪到它。順序由 `tools/code_emit/emit_order.py` 從 call graph 算出來，`next_batch.py` 照著發。

理由是測試的可信度。還沒 emit 的 callee 在連結時被填成回傳 0 的 stub（見下節），此時替 caller 寫的測試量到的是 stub 而不是真的 callee；照位址排會讓幾乎每支 function 都處在這個狀態。實測 514 支排完只剩 **11 對** caller 早於 callee，全部來自唯一一個 10 支 function 的環——環沒有 callee-first 的排法，這是定義使然，不是排序沒排好。

環裡的成員與那 11 對會被明白告訴 emitter：哪些 callee 現在還是 stub，測試就不准斷言依賴它們回傳值的東西。

## 資料還沒 emit 之前怎麼連結

**編譯只要宣告，連結才要定義。** 全域資料的定義由 [`data_emit.md`](data_emit.md) 的 pipeline 落地，一個全域在它的定義落地之前仍要讓每一輪連結得起來，所以建置**最多連結兩次**：

| | 帶什麼 | 產出 |
| --- | --- | --- |
| 第一次 | 只有 `src/` 與 `tests/` 的物件 | 未定義符號清單（`undefined.json`，落點見 [`tools/code_emit/`](../tools/code_emit/_index.md)） |
| 第二次（只在第一次有未定義符號時） | 加上自動產生的零填充 stub 模組 | 必須零未解析符號 |

第一次報出來的未定義符號**是預期產物，不是失敗**——它就是「已 emit 的程式碼要、而還沒有人定義」的完整集合，也就是**資料 emit 的權威工作清單**（[`data_emit.md`](data_emit.md)）。它每次建置重新產生，所以會隨進度自己縮短，而且永遠不可能與程式碼不一致。

這份清單現在是空的，所以第一次連結就解掉全部符號，不產生 stub 模組、也不跑第二次連結；這個機制留著當回歸檢查——任何新出現的未定義符號都會在第一次連結被報出來，並觸發帶 stub 的第二次連結。

stub 模組由 `tools/code_emit/gen_stubs.py` 產生：資料照 `tools/code_emit/data/routing.json` 記的型別與大小宣告（型別只為了對齊，內容一律是零），還沒 emit 的 function 給一支回傳 0 的空殼。**它不進 `src/`**，每次建置從頭產生、落在暫存區——stub 是「還沒有人下判斷」，放進 `src/` 就與真的 emit 出來的定義分不開了。

三類符號 `gen_stubs.py` 拒絕 stub，直接讓建置失敗：routing 不認得的名字（拼錯，或連結指令列漏了程式庫）、routing 標記為不 emit 的符號（字串字面值、區域陣列初值、switch 表——它們該在使用它的 function 裡面）、以及 `src/` 已經定義過的符號。

**stub 看不見的那一面：** 一個拼對了但拿錯的全域名字會被照樣 stub 成零，而零看起來很像一個合理的答案。這一類只有 reviewer 從 assembly 讀得出來，閘門讀不出來。

## emit 的 gate

`python tools/build_gate/gate.py check --target emittest`。`emittest` 目標把 `src/` 的全部生產程式碼與 `tests/` 的全部測試編譯連結成一個 DOS/4G 映像並在 DOSBox-X 裡實際執行。通過的條件是 build、errors、undefined、warnings 四項判定與測試套件都過；每一項怎麼判（連結器的 redefinition 算錯誤、未解析符號只看最後一次連結，沒有第二次時就是第一次、警告同時比文字與摘要行的數量）以 [`build_gate.md`](build_gate.md) 的「通過的條件」為準。

這個目標**不做映像等價比對**，判定欄位顯示 `not compared`，理由與其他目標的差別見 [`build_gate.md`](build_gate.md)。

emitter 自己在寫的當下跑的是 `python tools/code_emit/build_emit.py all`，同一套建置，少了閘門的自我測試套件。兩者的建置流程是同一份實作。

## calling convention 要在程式碼裡宣告

每一支 emit 出來的 function 都要在它的原型旁邊寫出自己的 calling convention，不靠 `-4s` 帶過：

```c
#pragma aux fdps_menu_find_first_enabled_entry "*" parm caller [];
```

`parm caller []` 就是堆疊慣例——引數全在堆疊、呼叫端清理，也就是 `-4s` 的預設。**實測這條 pragma 與不寫它產生逐 byte 相同的機械碼**（同一個 translation unit 加與不加各編一次，`.obj` 只差三個 byte，全部落在 Watcom 記錄來源檔時間戳的 COMENT 記錄裡，控制組重編一次也差同樣那三個）。所以它是純粹的宣告，不影響產出。

`"*"` 這一段是必要的：它把符號名固定成不加裝飾。**不能改用 `__cdecl` 關鍵字**，理由見 [`pitfalls.md`](pitfalls.md)。

## 八類隱性契約

前作 FD2 在實機階段修掉的八類 bug，共同點是**編譯全綠、單元測試全綠都驗不出來**——它們不是 function 算錯，而是 function 與環境之間的某個隱性契約與原版不一致。每一支 emit 的 function 都要逐條對過，不適用的也要說明為什麼不適用（[ADR-0003](../docs/adr/0003-manual-playtest-over-automated-golden.md)）。

| 類 | 隱性契約 | 檢查什麼 |
| --- | --- | --- |
| A | vendor function 真正破壞哪些 caller-saved register | `modify` 清單是精確集合語意。堆疊慣例下 EBX 是 callee-saved，只列一部分等於把污染搬家。四個 `eax ebx ecx edx` 要嘛全列要嘛不列 |
| B | 未初始化全域的擺放順序與相鄰關係 | Watcom 對 tentative 定義的順序與相鄰不保證，連同一個 `.c` 內也不照原始碼順序。任何讀寫經由鄰居位址碰到另一個全域的地方，兩者都要帶初值、落在同一個 `.c`、原始碼裡前後緊接，或 emit 成真的陣列或 struct，見 [`data_emit.md`](data_emit.md) 的「工具鏈怎麼擺全域」與「佈局約束」 |
| C | 全域與欄位的號性 | 號性是行為不是表示法，一進入比較就分歧。看 assembly 的 `JGE`／`JLE` 對 `JAE`／`JB` |
| D | 時序敏感熱迴圈的指令數 | 音效時長是實時的、繪圖時長隨模擬器 cycles 縮放，「等價而更短」的寫法會破壞原版依賴的時序平衡 |
| E | 硬編的絕對位址 | 重建版不會把任何東西擺回原位，寫死的位址指向的是別的東西。一律改用符號 |
| F | math intrinsic 的呼叫形式 | sqrt／sin／cos 走 intrinsic 還是走具名 CRT function 屬於 codegen，標頭檔的 pragma 是其中一部分。弄錯會把原版從未執行過的 vendor 程式碼帶進 runtime |
| G | 哪些 translation unit 帶 `__CHK` | 全開會被中斷的私有堆疊誤殺，全關失去溢位防護。原版的遊戲程式碼除了 CD 模組都是 `-s`；CD 模組以 `-os` 編譯、帶 `__CHK`（`__CHK` 的 34 個呼叫端有 32 支是 CD 模組的 function），見 [`build_flags.md`](build_flags.md) |
| H | 折疊後的基底歸給哪個符號 | 編譯器把常數索引折進位移後，Ghidra 會把基底歸給**前一個**符號。判準是「還原出的索引最大值超出宣告的元素數」。**build gate 完全看不到這一類** |

B、E、H 三類的共同症狀是「數值或指標讀到不相干的東西」。遇到這個症狀先問：這個讀取在原版是不是靠映像佈局才成立的。

## 測試怎麼組織

測試碼在獨立的 translation unit，連結生產程式碼但不修改它；`src/` 底下不存在任何條件編譯的測試 hook（[ADR-0003](../docs/adr/0003-manual-playtest-over-automated-golden.md)）。

| 規則 | 內容 |
| --- | --- |
| 一對一鏡像 | `tests/<stem>.c` 對應 `src/<stem>.c`，組語檔同理：`tests/<stem>.c` 對應 `src/<stem>.asm` |
| 沒有 C 介面的組語 | RLE 繪製的 kernel 只能經 `fdps_blit_dispatch` 進入，所以 `tests/rlebase.c`、`rlepal.c`、`rleturn.c`、`rlemix.c` 的每個 case 都呼叫分派者並指定該 kernel 的 mode，`tests/rledisp.c` 測分派者本身。這些 case 不依賴連進來的是組語還是 C 譯本，兩種都要過。C 譯本原有的直接呼叫測試留在 `tests/rle.c`、`rlecolor.c`、`rlerot.c`、`rleblend.c` 的 `#if 0` 參考區，檔頭寫明它們測的是參考用的 C 譯本，切回 C 版時一起重新啟用（[`code_layout.md`](code_layout.md)） |
| 註冊 | 該檔定義 `void run_<stem>_tests(void)`，用 `RUN_TEST` 登記每個 case。進入點由建置腳本從這個函式名產生，新增測試檔不需要接任何線；寫在 `#if 0` 裡的不算定義，不會被登記 |
| 遊戲進入點 | 測試映像的 `main` 是產生出來的那支，所以 `src/main.c` 在**測試建置裡**以命令列 `-dmain=fdps_game_main` 編譯，把遊戲的 `main` 改名避開重複定義；其餘 unit 不帶這個 define，`src/` 裡也沒有對應的條件編譯。遊戲本體的建置不加它。沒有測試呼叫遊戲的 `main`——它不是 `exit(1)` 就是跑進永不回來的主迴圈 |
| 斷言 | `CHECK_EQ`，見 `tests/testharn.h` |
| 涵蓋政策 | 風險導向：數值計算、分支結構、狀態轉移，以及任何用到「CALL 之後的回傳值」的地方必須測；純繪圖副作用可延後並註明 |
| 期望值來源 | 攻略站數值 > Ghidra emulator 對純計算取得的 ground truth > 從 assembly 手推。**禁止拿 emit 出來的 C 自己的行為當期望值**——那種測試只證明程式碼等於它自己 |
| 真實遊戲檔 | 要讀遊戲檔的測試必須讀真檔：把 8.3 檔名列進 `tests/gamefile.lst`，建置腳本會把它暫存到執行目錄。捏造結構假檔證明不了任何事 |
| 不准斷言 stub | 還沒落地的全域資料一律是 stub 的零、還沒 emit 的 callee 一律回傳 0。**任何期望值取決於這兩者的斷言都不成立**——它今天會過，等真值落地那天變紅，而那是最糟的發現時機。行為由靜態表決定的 function 在測試內自備局部 fixture 表，不得為了測試提前 emit 該符號的真值。現在兩種 stub 都不存在：全域的值是原版映像的初值（`tools/data_emit/data/manifest.json`），function 也全部 emit 完，這條規則留給之後新加入的符號 |

`src/fdpstype.h` 的 23 個遊戲 struct 由 `tests/fdpstype.c` 逐欄檢查：每個 struct 的 `sizeof` 與每個欄位的 `offsetof` 都對照 `ghidra_snapshot/data_types.txt` 記的偏移。期望值來自快照而不是標頭，所以它證明的是「編出來的佈局等於原版的佈局」，不是「標頭等於它自己」。兩個檔都是 `tools/code_emit/gen_types.py` 的產生物，不手改。

**Case 結束前要把「載入器會 free 的全域」放回 bss 狀態。** `data_fdps_map_unit_array_ptr`、`data_fdps_cel_sprite_cache_ptr`、`data_fdps_command_sprite_sheet_ptr` 這一類全域，生產程式碼會直接 `free`（`fdps_load_field_chapter_resources`、`fdps_build_map_unit_array`、`fdps_shutdown_free_resources`），而且兩支載入器的守門條件是配對的計數不為零，不是指標不為 NULL（`fdps_load_field_chapter_resources` 對 map unit array 另要求指標不等於 roster 陣列，指向測試 static 時照樣成立）；`fdps_shutdown_free_resources` 則是指標不為 NULL 才 free 前兩個，`data_fdps_command_sprite_sheet_ptr` 完全不守。測試 case 把它們指向測試檔裡的 static 之後就離開，後面任何一個呼叫這些載入器的測試都會對非堆積記憶體做一次 `free`，配置器從此壞掉。症狀落在幾百個 case 之後、與肇因無關的檔案裡（`_heapchk` 回 `_HEAPBADNODE`、堆積走訪數到 0、最後整支測試程式卡死），而且會隨連結佈局漂移：今天全綠只代表那個 static 前面的位元組剛好不像堆積節點，下一個 function 落地就可能翻臉。計數歸零、指標設 NULL 就是那些守門條件當初假設的初始狀態，`free(NULL)` 是 no-op，所以還原不會讓下游少掉任何東西。

**會播音效的畫面，fixture 要自己擺一個空的效果容器，而且不能靠別的測試檔留下的指標。** `fdps_play_sfx` 到 `data_fdps_audio_basewav_sfx_bank_buf_ptr` 指的容器裡查名字，而查表（`fdps_vfs_image_get_entry`，`src/vfs.c`）完全不測指標，一進來就把 header 從那個位址複製出來：**NULL 不是「查不到就安靜跳過」，是把低位記憶體當成 entry count，再照那個數字走幾億筆假 entry**——看到的症狀是整支測試程式停住，不是少一個音效。所以凡是會開選單環、訊息視窗或戰鬥演出的 case，stage 時就要把一個全零的 `struct fdps_vfs_image_header` 指過去（entry count 為 0 讓查表乾淨落空），離開時設回 NULL；`tests/menu.c`、`tests/statwin.c` 是樣本。沒有自己擺的檔在今天的檔案順序下可能剛好接到上一個測試檔留下的有效容器，於是一直是綠的，等到拆檔或改名換掉順序、或上一個檔改成離開時設 NULL，它就在完全無關的地方掛死。

**兩個 guest 的 memsize 不一樣，而且必須不一樣。** 工具鏈 guest 是 32 MB（`tools/fdps_build/build_min.py` 的預設）；跑測試的 guest 是 64 MB（`tools/code_emit/build_emit.py` 的 `RUN_MEMSIZE_MB`）。工具鏈那邊不能跟著調大：DOS 版 `wcc386` 在 64 MB 之下每個 unit 都以 `A PAGE FAULT HAS OCCURRED DUE TO INSUFFICIENT MEMORY` 收場，連編譯階段都過不了。執行那邊則非調大不可，因為 `tests/anim.c` 每個 banner case 都把整份 27.4 MB 的 MISC.VFS 讀進一次 `malloc`：在 32 MB 之下整支測試程式跑在離上限約 1 MB 的地方，**EMITTEST.EXE 只要再長 7 KB 就會翻臉**——不分那 7 KB 是 static buffer、是資料還是純程式碼，也不分新加的測試有沒有被執行到。翻臉的樣子是 `fdps_animate_turn_banner` 那個不檢查回傳值的 `malloc` 拿到 NULL 之後照樣寫過去，DOS/4GW 的 IDT 被蓋掉，DOSBox-X 以 `E_Exit: Illegal descriptor type 0 for int 8` 結束，**症狀落在 `tests/anim.c` 裡，與剛加進來的那個檔案毫無關聯**。

**即使有了 64 MB，整張畫面的快照仍然走堆積、不要放大型 static buffer。** 需要整張畫面的 case 就在跑之前 `malloc`、跑完 `free`，那段時間大檔案沒有被握著；`tests/gauge.c` 與 `tests/unitatk.c` 是這個寫法的樣本。static 版本（仍散在十幾個測試檔裡，`tests/statwin.c`、`tests/save.c`、`tests/anim.c` 最多）只是把餘裕吃掉，餘裕吃完了下一個落地的 function 又會撞上同一道牆。

**手塞 BIOS 鍵盤環形緩衝區之後，要等 timer tick 前進一次才查鍵盤。** DOS/4GW 之下，只要同一個 timer tick（`0x46c` 的值，最長 55 ms）內有任何一次鍵盤查詢——CRT 的 `kbhit`（原版 `00043570`，先看 ungetch 位元組 `0x60440`，否則走 `INT 21h` AH=0Bh）或 `INT 16h` AH=01h——答過「沒有鍵」，之後的每一次查詢都照樣答「沒有鍵」，不管 `0x41a`／`0x41c` 的頭尾與環裡寫了什麼，直到 `0x46c` 下一次前進。同一支程式編成不經 extender 的 real-mode `.EXE` 則每次第一次查詢就看得到，所以這個「記住沒有鍵」發生在 DOS/4GW 的保護模式路徑上，不在 BIOS 資料區。量測腳本是 [`tools/kbd_probe/`](../tools/kbd_probe/_index.md)。真的按鍵帶著自己的 IRQ1，不受影響；受影響的只有手寫環形緩衝區的測試。

所以以手塞環形緩衝區當前提的 case：塞完之後、查詢之前，**不做任何鍵盤查詢地**等 `0x46c` 變一次（要有上限，等不到就讓斷言失敗而不是卡死），然後才可以無條件斷言「看得到」與「跑完就排空」。不等的話，結果取決於前面某個 case 最後那次空查詢有沒有剛好落在同一個 tick，同一支執行檔重跑會時紅時綠。`tests/title.c` 的 `movie_wait_for_timer_tick` 是樣本。

**`__LINE__` 在 `CHECK_EQ` 裡不可用。** wcc386 10.0a 只有在巨集呼叫位於行首時給出正確的行號；跟在同一行其他 token 後面時給的是前處理後串流的行號，會落到檔案結尾之外。所以失敗訊息用「測試名稱＋該測試內的第幾個檢查」定位，不用行號。

## 工作狀態與續跑

`tools/code_emit/data/emit_state.json` 是進度的正本，進版控。一個 function 一筆，`status` 從 `pending`（或沒有紀錄）開始，emitter 動手前寫成 `in_flight`，通過審查與閘門後由 bookkeeper 直接寫成 `committed`；未通過的記成 `failed`，判定為不 emit 的記成 `skip`，兩者都必須出現在收尾報告裡；被中斷而留下的 `in_flight` 由下一輪的復原段改寫成 `interrupted`。只有 `committed` 與 `skip` 是終態，會讓 `next_batch.py` 把該位址從工作清單移除；其餘（含 `failed` 與 `interrupted`）一律重發。

**「已完成」不能只看狀態欄。** 每筆記下 emit 當時所依據的 Ghidra body size，續跑時與 [`ghidra_snapshot/functions.txt`](../ghidra_snapshot/_index.md) 比對；function 後來變了大小，舊的 emit 描述的是已經不存在的程式碼，該筆退休並重回工作清單。**沒有記下依據的那一筆也算過期**——「沒人寫下這是照哪一份程式碼做的」不是「程式碼沒變」的證據，當成證據就會讓那筆永遠被跳過。

**判定為不 emit 的 function 也要寫回狀態檔。** 反編譯器碎片、連結器產物之類的東西是一個結論，不是一次失敗；不記下來的話，下一次跑工作清單又會把同一個位址發出去，而且每次都要燒掉一次完整的三源閱讀才能再得到同一個結論。

**狀態檔裡沒有 commit 雜湊。** 雜湊沒辦法寫進產生它的那個 commit 裡，事後 `--amend` 補上去只會得到一個指向被丟棄物件的雜湊。落地 commit 的標題帶著位址，用 `git log --oneline --grep "@ <addr>"` 找。

每一支通過的 function 是一個獨立的 commit，所以任何中斷最多只損失飛在半空的那一支。未通過的 function 由 workflow 清掉工作區的殘留並把狀態記成 `failed`——留著半成品的話，它會出現在下一支 function 的 diff 裡並被當成別人的改動 commit 掉。

## 中斷復原：開跑前收拾，不是失敗時才收拾

「最多只損失飛在半空的那一支」是對**下一次**的承諾，而它只有在 workflow 第一段先收拾工作區時才成立。撞到 usage limit 不會丟出任何腳本攔得到的例外，它是把 session 就地殺掉：失敗路徑上那個清工作區的 agent 從來沒有機會執行，半成品原封不動留在 `src/` 與 `tests/` 裡。下一支 function 的 reviewer 看到的 diff 混著前一支的殘骸，bookkeeper 的 `git add src tests` 把它一起 commit，而 commit 標題寫的是別人的名字——損失的不是一支，是兩支。

**唯一保證在「session 已經死過一次」之後還執行得到的時機，是下一次開跑的第一件事**，所以收拾放在那裡：取工作清單之前先看工作區，丟掉殘骸，把對應的 function 記成 `interrupted` 送回工作清單，然後才開始。

**能碰的東西由路徑決定，且不容協商。** 界線就是這條 pipeline 自己的各段會寫的那六個路徑，每一個都可以丟掉重做：

```
src/  tests/  tools/code_emit/data/  ghidra_snapshot/
tools/code_emit/build_routing.py  rebuild_info/code_layout.md
```

`workspace/` 不能碰——判定檔是中斷現場的紀錄，重跑要讀它；已經 commit 的東西不能碰，不 revert、不 amend、不移動 HEAD；界線外的路徑髒了就**停下來報告**，那不是這條 pipeline 的殘骸，猜它是什麼就是在刪別人的一個下午。`src/`／`tests/` 髒了卻沒有任何 `in_flight` 認領它，照樣丟——界線是路徑不是歸屬——但要在報告裡講明殘骸沒有名字。

`ghidra_snapshot/` 是唯一不能單純還原的一個：中斷前 bookkeeper 可能已經改了 Ghidra 並存檔，把文字快照還原成 HEAD 只會讓它描述一個不存在的資料庫。所以那一項髒的時候是**重新匯出**而不是還原，匯出來什麼就是什麼。

**清理要先 `git reset -- <路徑>`。** reviewer 為了讓新檔出現在 diff 裡會跑 `git add -N -- src tests`，而 intent-to-add 的檔案在 index 裡：`git checkout --` 只會把它截成 0 byte 而不是移除，`git clean -fd` 又把它當 tracked 而跳過，結果留下一個 0 byte 的 `.c` 被下一支 function 的 bookkeeper commit 成自己的。三個指令要湊齊：pathspec 的 `reset`（只 unstage，不動 HEAD）、`checkout`、`clean`。

**足跡故意不 commit。** 每支 function 的 emitter 動任何東西之前先把該筆寫成 `in_flight` 並帶上時間，這一筆留在工作區不落 commit：落地 commit 會把它推到 `committed`，所以它永遠不會活過一支跑完的 function；反過來說，開跑時還讀得到 `in_flight` 就代表上一輪死在那一支。這是「工作區髒」之外唯一的線索，而工作區正是要被清掉的東西。`in_flight` 與 `interrupted` 都不是終態，重發規則與 `pending` 完全相同——那一支從頭到尾沒有被判定過任何事，沒有東西需要保留。

**界線是雙向的：pipeline 自己也只准把那六個路徑弄髒。** 收拾段靠路徑分辨殘骸與工作，所以任何一段做出界線外的改動——建置腳本、gate、其他知識庫頁、devlog——都必須**當場單獨 commit 掉**，不能留在工作區，也不能讓它搭上 function 的落地 commit。留著會讓下一輪的收拾段判定成界線外而停下來等人，正好是這條 pipeline 存在的目的的反面；搭順風車則是把一個沒被 review 也沒被 gate 過的改動塞進一個寫著「reviewer 通過、build gate 通過」的 commit 裡，之後沒有人找得到它。兩個誠實的 commit，不要一個不誠實的。

這條對收尾那一段同樣成立：**批次結束寫的 devlog 與 run report 要自己 commit 掉**，否則一批跑完就留下界線外的髒路徑，下一批一開跑就停。

實作上有兩個順序陷阱：

- **界線外的東西要在 `git add src tests …` 之前先處理掉，而且 commit 要帶 pathspec。** git commit 的是 index 不是你的意圖：function 已經 staged 之後再 `git add <那個檔> && git commit`，會把整支 function 一起 commit 在那個檔的標題底下，然後真正的落地 commit 因為「沒有東西可 commit」而失敗。寫成 `git add <path> && git commit -- <path>`，並且擺在 stage function 之前。
- **判定「清乾淨了」要同時看 commit 與工作區。** 只確認狀態檔 commit 成功、不看工作區是否還髒，等於沒清——殘留會被下一支 function 的 `git add src tests` commit 成它的。

**收尾報告要分得出兩種沒落地。** 「還沒輪到」是 run 在它之前就停了，什麼都沒動、沒有東西要清；「跑到一半被中斷」是工作區裡有東西，要靠下一輪的收拾段處理。合成一個數字就是讓後者被當成前者，於是沒有人去看工作區。

## 批次大小是呼叫者的決定

workflow 不看自己的預算。它跑完呼叫者給的清單為止，停下來的理由只有這幾種，沒有一種是預算：清單跑完、上游失效（[ADR-0007](../docs/adr/0007-workflow-automation-and-agent-context.md) 5.2／5.5）、外力中斷，以及工作區不乾淨——開跑時界線外有髒路徑或收拾後仍不乾淨，或放棄一支 function 後清理沒有清乾淨。

理由是職責：呼叫者說要跑 40 支，workflow 依一個沒人要求它套用的門檻在第 12 支收手，是在回答沒有人問的問題。預算耗盡的正確表現是被外部殺掉，而上面那段收拾機制就是讓「被殺掉」變成可承受的東西。一批跑完由呼叫者檢查結果、修掉問題、直接呼叫下一批，全程不需要使用者介入。

資料 emit 的 workflow（`tools/data_emit/emit_ticket23.js`）是自己寫的一支，照這一節做——[ADR-0007](../docs/adr/0007-workflow-automation-and-agent-context.md) 說不抽共用骨架，共用的是原則；它與本頁不同的界線與足跡見 [`data_emit.md`](data_emit.md)。

## 檔案落點

每個 function 與資料符號的目標 `.c`／`.h` 由 [`code_layout.md`](code_layout.md) 擁有，逐項對照表是 `tools/code_emit/data/routing.json`。emit 期間不做落點判斷：工作清單本身就帶著目標檔。

`emit_state.json` 只記進度，不記落點；兩邊對某支 function 的目標檔不一致時 `next_batch.py` 報錯而不是二選一。
