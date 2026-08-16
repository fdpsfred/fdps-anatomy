# CRT 與 AIL 改用程式庫的原名（票 14.1 的後續）

問題是使用者問出來的：「crt function 現在有依照 fd2 的命名方式嗎？名稱必須跟在 watcom crt library 內完全一致，之後 emit code 以後才可以直接編譯和 link watcom crt library。」

答案是沒有，而且錯得比想像中深。

## 我們抄錯了一句摘要

`rebuild_info/naming.md` 寫的是「`crt_` + 程式庫符號原樣」，來源是 `docs/research/fd2-playbook.md` 第 243 行：「vendor 前綴 | `crt_` / `crt_equivalent_` = Watcom CRT 層」。那一行是壓縮過的摘要，而它壓錯了。

前作真正的正典是 `rebuild_info/crt/symbol_inventory.md` 與 `lookup_9.5a.json`。把那份 lookup 的 194 筆拿出來數：**0 筆帶 `crt_` 前綴**。186 筆是 Watcom 原名（`malloc`、`printf`、`__CHK`、`__STKOVERFLOW`、`IF@COS`），8 筆是 lib 內匿名 static 的 `L$1_stk_save_ss` 這種形式。`crt_` 只用在 `crt_equivalent_*`——那是「行為等價但 byte 比對不上任何 lib obj，必須手寫」的那一類。

前作把理由也寫了：「命名以能被 Watcom linker 直接解析為目標」。這正是使用者講的那件事。

值得記的是 FD2 自己的 `src_map.md` 也有同一句壓縮過的「`crt_` / `crt_equivalent_`」——所以錯不是抄的時候發生的，是抄了一份本來就不精確的摘要。**跨專案沿用慣例時，要去讀那個慣例的正典檔，不是讀導覽表格。**

## 對帳：203 個名字裡有 14 個是錯的

拿 `wlib -l` 把 `CLIB3S`／`MATH387S`／`EMU387`／`CSTRTX3S` 的 811 個公開符號抓出來，跟 Ghidra 裡 203 個 `crt_*` 名字對：

- 191 個去掉前綴之後逐字命中 PUBDEF——純粹是前綴的問題。
- 12 個對不上。其中 `crt___stkoverflow` 是大小寫錯（`__STKOVERFLOW`）；`crt_cstart` 該是 `_cstart_`；`crt_cmain`／`crt_init_rtns`／`crt_init_delay` 是自己編的描述性名字，而 FID 早就給了 `__CMain`／`__InitRtns`／`__delay_init`。
- `COS`／`SIN` 看起來連前綴都沒有——**但那是我自己的稽核腳本造成的假象**，見下面「正則吃不下 `@` 和 `$`」。Ghidra 裡實際是 `crt_IF@COS`／`crt_IF@SIN`，去掉前綴之後就是前作也用的 `IF@COS`／`IF@SIN`。

最糟的一個是 `crt_sprintf`：它掛在 `000435a2`，而 FID 對那個位址的最佳候選是 `spawnlp`（26.7）；真正的 `sprintf` 在 `00042d41`，分數 265.7，卻因為名字被佔走而被迫叫 `crt_sprintf_00042d41`。一個錯名把另一個對的名字擠掉了。

## 順手做完 AIL

既然 vendor 的名字要照抄程式庫，AIL 就有現成的權威來源：票 14.1 剛建好的 `ailv3.lib` FID 比對。位元組相同、單一候選、名字沒有第二個人在爭的，直接照抄。結果是 252 個 AIL function 改名或補名，包含混音分派表那 134 個 callback——`AIL_internal_mix_finalize_<slot>` / `_mix_loop_<slot>` 這套命名規約不用自己想，前作的庫裡就是這麼寫的。

同時修掉的偏差：FDPS 之前用 `_impl` / `_worker` 當 inner worker 的後綴，前作用的是 `AIL_internal_<公開名>_inner`。比對認得出來的當場換掉；剩下 8 個 FID 咬不準的（`_impl` 那批）走 workflow 逐一定名，每一個在 `ailv3.lib` 裡都有同名的 `_inner` 對應。

`0004adb0` 與 `00041f9a` 這對是最好的例子：前者原本叫 `AIL_release_channel`，後者叫 `AIL_release_channel_00041f9a`。比對說反了——`0004adb0` 是 inner worker（408.8），`00041f9a` 才是公開的 `AIL_release_channel`（242.0）。兩個對調。

## 四個沒想到的坑

**正則吃不下 `@` 和 `$`，於是稽核在說謊。** 從快照的 prototype 撈函式名用的是 `\b(\w+)\(`，而 `\w` 不含 `@` 與 `$`——`IF@COS` 讀成 `COS`，`L$1_stk_save_ss` 讀成 `1_stk_save_ss`。這正是本專案的命名規則**要求**使用的兩種形狀，所以錯得剛好最痛。後果有兩層：稽核把 `crt_IF@COS` 報成「叫 `COS`，不合任何命名規則」而把它推進判定清單；更糟的是我照著這個假象在 devlog 寫下「有人把 `IF@` 拿掉了」，一個工具產物就這樣變成了記錄裡的事實。改用 `([A-Za-z_$@?][\w$@?]*)\(` 之後全部消失。

如果 `mechanical.json` 的 `from` 欄位被這個正則寫壞，`ApplyLibrarySymbolNames` 會比對不上而拒絕整批改名——這次沒踩到只是因為那幾個位址剛好都先掉進判定清單了。

**Ghidra 不擋重複的 function 名。** 機械改名跑完之後掃了一次全程式重名，`itoa` 出現兩次：`00054aec`（53 byte，FID 71.4，單一候選）跟 `00054bf5`（26 byte，`_ltoa`/`_itoa` 平手在 34.68）。判定 agent 只看得到自己那一個 function，看不到別人剛拿走什麼名字，而 Ghidra 收下重複名字時一聲不吭。修法是把重複偵測寫進 apply 階段的提示——改完名要主動去查有幾個人叫這個名字，因為沒有東西會替你查。附帶一提，`search_functions_enhanced` 在這個 session 對任何 pattern 都回 0 筆，agent 自己換成 `search_functions` 才驗成功。

實際答案由版面決定：`utoa`／`itoa`／`_itoa`，然後 `ultoa`／`ltoa`／`<這一個>`——每個轉換器後面跟一個 0x1a 的同伴。所以 `00054bf5` 是 `_ltoa`。

**改名會連帶暴露 pool 判錯。** 5-byte 的 `JMP` island `0003da44`／`0003da49` 原本歸 `binary_artifact`，理由是「沒有函式庫符號、沒有 FID 命中」。但 5 byte 在 FID 的門檻（4 個 code unit）以下，**它從來沒有被問過**——「沒命中」在這裡不是證據。而 `ailv3.lib` 明明白白把這兩個 thunk 當公開符號收著。三個 island 因此改判（`0003d370`→`crt`，另兩個→`ail`），`binary_artifact` 從 11 掉到 8。這是票 14.1 那個 `0003dc2f` 的同一條界線又出現了一次。

**先刪再寫會弄丟狀態。** `build_contradiction_worklist.py` 開頭就把整個資料夾的 `*.json` 清掉，包括存著 sticky `--extra` 清單的 `worklist.json`。中途 crash（我加 `re` import 之前那次）就把清單一起帶走了，而且是永久的。改成全部建好再刪再寫。

## 最後的狀態

| pool | function | byte |
| --- | --- | --- |
| `fdps` | 517 | 181,999 |
| `ail` | 442 | 57,873 |
| `crt` | 380 | 42,124 |
| `binary_artifact` | 8 | 47 |

446 個機械改名 + 29 個逐一判定。全程式沒有 `crt_` 前綴殘留（`crt_equivalent_` 除外，目前 0 個）、沒有 `crt` pool 的名字不在 PUBDEF 清單裡、沒有 AIL 名字不合慣例。唯一的重複名是 Ghidra thunk 的別名機制（`0003d50a` 顯示目標的 `__sys_init_387_emulator`），那是模型本身的表現方式，不是存了兩個同名符號。
