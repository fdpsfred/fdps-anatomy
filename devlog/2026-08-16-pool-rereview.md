# 票 14.2 的複核：把剩下的 132 個未定案跑完

票 14.2 是把票 14 判過的每個 function 再讀一次：一個 agent 一個 function，四條互不相干的軸——pool、name、boundary 與 signature、plate comment。這一篇記的是最後這一段續跑，不是整張票。前面幾輪把 1,344 個 function 走完，留下 132 個「本票自己的軸還在懷疑」的判定，這一輪只做這 132 個。

所以整輪跑的都是 rescan 段，沒有跑到任何一個新 function。這件事後面會反覆出現，因為驅動腳本的統計欄位不是為這種形狀設計的。

## 結構：先讀證據，再讀答案

這張票唯一特別的設計是把證據與既有答案拆成兩份檔案。`items/facts/<addr>.json` 是 byte、body range、call edge、reference、library hit；`items/current/<addr>.json` 是 Ghidra 現在掛著的名字、tag、signature、plate。提示明確排定讀取順序，agent 必須先形成自己的結論，才准去看票 14 寫了什麼。理由是「複核」很容易安靜地退化成「替既有標籤找理由」，而那種退化在報告裡看不出來。

第二條是 rescan 段——也就是這一輪——是**唯一**允許讀別人判定檔的階段。每個 agent 只看到一個 function，凡是身分取決於鄰居的問題（誰 dispatch 這張表、這個 symbol 到底屬於哪個 body、caller 到底推了幾個參數）在第一遍都無解。這一輪的整個價值就在這裡：`rescanPrompt` 明講「讀鄰居的判定是在使用別人已經下好的判斷，不是重判他，而且永遠不准改寫別人的判定檔」。

第三條是 boundary 修正會 retire 周圍的 function，所以工作清單在跑的途中可以長大。**這一輪它一次都沒有觸發**：boundary 修正 0 個，兩輪落地的 `boundary_changes` 都是 0，整張票到目前為止 `changed_counts.boundary` 也是 0。這個機制寫了、測不到，就是它的現況。

## 死路一：四十份報告的第一段都在拆同一個假訊號

每個 rescan agent 收到的提示裡有一行「Why it came back」。這一輪絕大多數收到的是 `the pool came back unknown`。

它是假的。`rereview_ticket14_2.js` 的 `rescanFromDisk` 把從磁碟撿回來的 leftover 一律塞成 `verdicts.push({ addr: id, pool: 'unknown', confidence: 'low', ... })`（約 769 行），而產生理由字串的地方（約 822 行）就照著這個 placeholder 講話。磁碟端真正的判準寫在 collect 的那支 python 一行式裡：**五條軸有任何一條低於 high，或者根本沒有 pool**。

進場時的實際狀態（`summary.json`，這輪開跑前的快照）是：132 筆 unresolved 裡 pool 是 crt 59、ail 48、fdps 25，一個 unknown 都沒有；全庫 axis confidence 是 pool 只有 2 筆 medium、boundary 2 筆、plate 9 筆，而 name 64 筆、signature 65 筆。也就是說這一輪要清的幾乎全是**名字與簽章**，pool 早就定了。

代價是實打實的。翻這一輪的 agent 回報，開頭在拆這個假訊號的至少有四十份，句式各異但講的是同一件事：「the pool was never actually unknown」「a collector artifact」「the driver's placeholder」「a reporting slip」。有幾份還順手指出行號，也有一份直接建議把它修掉，免得誤導下一輪的 agent。這是純浪費：每個 agent 花掉開場的推理預算去否證一個腳本自己編出來的前提。

好的一面是它們沒有被騙。沒有任何一份報告因為提示說 pool unknown 就真的去重判 pool。壞的一面是這種浪費不會出現在任何統計裡。

**要改的**：`rescanFromDisk` 不該偽造 pool，理由字串應該講真正的判準（哪一條軸低於 high），或者乾脆不講理由，讓 agent 自己從判定檔讀。

## 死路二：統計說「什麼都沒改」

跑完的 run statistics 是這樣的：

```
"changedByAxis": {},
"changedFunctions": [],
"boundaryFixes": [],
```

第一眼會讀成「第二遍讀完，一個判定都沒推翻」。這個讀法是錯的，而且危險。

腳本結尾組 `stats` 時，`changedByAxis` 與 `changedFunctions` 都來自 `s.changed_axes`，那個欄位只有 Review 段的 agent 會填。rescan 段收結果的地方只動 `target.confidence`、`target.has_open_question`、`target.pool`，從頭到尾不碰 `changed_axes`。所以**一輪純 rescan 的跑，這兩個欄位必定是空的**，跟實際改了多少無關。

實際改了多少，只存在於 `rescanResolved` 那 130 段自然語言裡，以及磁碟上 129 個被改寫的判定檔中。這一輪沒有任何機器可讀的逐軸變更計數，所以本篇也不寫逐軸的推翻數字——寫了就是編的。

**要改的**：rescan 段收結果時要把 agent 回報的變更軸記進 `changed_axes`，或者統計改成直接掃判定檔的 `_supersedes`。

## 死路三：一個 confidence 純量對五條軸

腳本回報 `stillOpen` 三筆：`0003b8a0`、`0003b9f0`、`0004515e`，而且都標成 `(unknown)`——又是同一個 placeholder。

拿磁碟端的判準重跑一次，現在還不過關的是 **21 筆**，不是 3 筆：

| 卡住的軸 | 位址 |
| --- | --- |
| signature | `00021030` `0003a640` `0003ad80` `0003b8a0` `0003b9f0` `00044fe4` `0004515e` `000477f4` `00047e0a` `0004b370` `0004bced` `0004bde5` `000516c6` `00054917` |
| name | `000431ef` `00045f50` `000463c0` `0004ceff` `0004d4f1` `0004d605` `0004dca3` |

其中 14 筆是**這一輪自己改寫過**的檔案。差距的來源是 schema：agent 回報的是一個 `confidence` 純量，判定檔裡是五個逐軸 confidence。一個 agent 完全可以誠實地把它被叫回來的那條軸解掉、回報 confidence high，同時在檔案裡留下另一條軸是 medium。`0004bde5` 就是教科書例子：它把名字從 `FUN_0004bde5` 定到 `__set_ERANGE`（靠 `seterrno.obj` 的 PUBDEF 加上 errno.h 的常數對映，沒有自由度），然後在報告裡明講「cc still medium since a zero-arg void body cannot distinguish `__cdecl` from `__watcall`」——這是正確的誠實，不是疏漏。腳本的純量表達不了。

所以 `stillOpen: 3` 低報了。真正的剩餘是 21 筆，而且形狀很集中：14 筆是零參數 void body 的 `__cdecl`／`__watcall` 標籤，7 筆是 CRT file-static 的 `L$` 名字。兩堆都不是判斷不出來，是還沒有人統一決定。

**要改的**：rescan 的 schema 應該收五個逐軸 confidence，`stillOpen` 用它算。

## 真正的收穫一：Function ID 結構上看不見 file-static

這一輪最大的方法論突破來自 CRT 那 60 個。

一路以來，凡是 Function ID 沒命中、又推得出所屬模組的 CRT function，都照 `naming.md` 給一個 `L$N_<module>_<purpose>` 的自造名字。這一輪有一個 agent 在處理 `00045c92` 時決定不要編，直接去翻 `CLIB3S.LIB` 的 byte：58 個 byte 在 `asctime` 模組的檔案 offset 0xa71b 逐字相同，而那個模組裡有一筆 **LPUBDEF** 記錄，把這個 offset 叫做 `convDec`。

關鍵是：**Watcom 把 file-static 寫成 LPUBDEF，而 Function ID 索引的是 PUBDEF。** 所以 FID 不是「沒認出來」，是結構上不可能認出來；`wlib` 的 listing 也看不到，因為它列的也是 public。而 library 檔案裡一直有真名。

這條路後來被好幾個 agent 各自走到，方式有三種：直接 parse OMF 記錄、`wlib` 抽模組再 `wdisasm -l`、以及純 byte 搜尋定位模組後讀符號表。收回來的真名（全部有 byte 比對佐證）至少有：

- `asctime` → `convDec`
- `timeutil` → `calc_yday`、`check_order`、`time_less`
- `efgfmt` → `forcedecpt`
- `ioexit` → `docloseall`
- `prtf` → `FixedPoint_Format`、`far_strlen`、`fmt4hex`、`float_format`、`formstring`、`zupstr`、`getprintspecs`、`evalflags`
- `tzset` → `tryOSTimeZone`、`parse_time`、`parse_offset`、`parse_rule`
- `MATH387S` 的 `ftos` → `DoEFormat`、`DoFFormat`、`AdjField`
- `stack386` → `stackavail`（9 個 byte，整個模組只有這一段程式碼）
- `gtpid` → `getpid`（6 個 byte，OMF 記錄只有 EXTDEF＋LEDATA＋PUBDEF 三筆）

處理 `00054cf7` 的那份報告順手數了一下：CLIB3S 有 64 個模組帶 LPUBDEF，共 123 個名字。也就是說目前掛著 `L$N_` 或 `L$1_` 的那二十來個名字，多數的真名就在那些記錄裡等著。這件事本身不屬於這張票（票 14.2 判 pool 與現有名字對不對，不負責重新命名整個 CRT），但它把後面那張命名票的做法整個換掉了：**先去讀 library 的 LPUBDEF，不要先造名字。**

順帶一提，`getpid` 那一筆還撞到一個真的形狀碰撞：CLIB3S 的 `__execaddr` 模組有 byte 完全相同的程式碼（只差 fixup 指到 `__Exec_addr` 而不是 `_psp`）。是靠 `0x0006042c` 的另外五個參照全部在 startup body 裡讀寫、以及 caller `__MkTmpFile` 拿它去組 `tXXXX_YY.tmp` 才排掉的。六個 byte 的 body，形狀證據等於零。

## 真正的收穫二：對得上前作，但雜湊對不上

AIL 那 47 個的主軸是另一件事：**「FID 沒命中」不等於「前作的 lib 裡沒有這個 function」。**

`ailv3.lib` 是前作從 `FD2.LE` 合成出來的，兩份 binary 是同一套原始碼的不同連結。body 差一個 byte，full hash 就完全不同，FID 一定靜默地沒命中。這一輪有一批名字是靠**位置對應**而不是雜湊撿回來的：

- `00048240` → `AIL_internal_xmidi_find_chunk`。整片鄰居位移固定 0x6070；前作記錄這個模組呼叫 `strncmp` 五次，entry offset 0x1b/0x2e/0x7d/0x91/0xc0，這個 body 一模一樣。差別只有 281 對 282 個 byte。
- `000486a0` → `AIL_internal_sequence_handle_midi_event`。前後兩個鄰居都有 FID 命中且 body hash 與 lib 逐字相同，而在 `ail_code.obj` 裡那兩個之間只夾著一個 function。entry 到 entry 兩邊都是 1,104 byte，雜湊沒中只是因為 Ghidra 在兩個程式裡把 body 與 padding 的界線畫在相差四個 byte 的地方。
- `00047884` → `AIL_internal_voc_dispatcher`。逐指令比對前作的 `ail_code.bin`，全部差異集中在格式檢查附近的 codegen（`XOR EAX,EAX; MOV AL,[EDI+9]; CMP` 對上 `CMP byte ptr [EDI+9]`），總共 22 個 byte。
- `000497f0` → `AIL_internal_mdi_driver_setup_full`，`000469f0` → `AIL_internal_dig_driver_setup_full`（後者 1,546 對 1,547 個 byte，就差一個），`00047b01` → `AIL_internal_wav_chunk_dispatch`，`00045384` → `AIL_internal_API_read_INI_inner`，還有 `sequence_status/tempo/volume_inner` 那一族。

`0004a7a0` 那組特別乾淨：lib 的 body 是 12 個 byte `8b 44 24 04 85 c0 74 03 8b 40 44 c3`（TEST/JZ 跳過載入），這個映像是 13 個 byte（TEST/JNZ 跳過一個提早的 RET）。語意相同、長度差一、雜湊必不同。

同時這一輪也**推翻了幾個第一遍自造的 AIL 名字**：`AIL_register_MDI_driver` 根本不是 Miles 或 `ailv3.lib` 發布的符號（連結時解不掉），`AIL_internal_install_DIG_driver_common`、`AIL_internal_apply_wav_image`、`AIL_internal_voc_block_dispatcher` 都是規則產生的合法但杜撰的名字，全部換成 library 自己的拼法。CLAUDE.md 那條「動手前先查前作」在這裡被違反過一次又補回來——第一遍的 agent 是照命名規則造名字，沒有去翻前作的 inventory。

## 被推翻的前作結論：`00044dc0`

前作的知識庫把 `00044dc0` 這四個 byte（`PUSHFD; POP EAX; CLI; RET`）記成 `crt_equivalent_get_eflags`，說是「Watcom 的 `_disable` primitive」。這一輪把它否掉了，而且否得很硬：Watcom 真正的 `_disable` 是兩個 byte `FA C3`（`CLIB3S.LIB` 的 `disable` 模組），而 `9C 58 FA C3` 這個序列在 Watcom 10.0 到 10.6a 的 **1,135 個 `.lib`／`.obj` 裡一次都沒出現**，含 CLIB3S／MATH387S／EMU387。crt 這個讀法沒有任何 byte 支持。

反面的解釋也找到了：前作合成 `ail_code.obj` 時把這四個 byte 從中間挖掉（`ail_layout.json` 的 offset 直接跨過去），所以 `ailv3.lib` 根本不發布這個 body 的符號，`fid.ail` 永遠不可能命中。而它夾在兩個 FID 命中的 `ail_code.obj` body 中間、沒有空隙，`FD2.LE` 也重現同樣的 69／4／21 byte 佈局。

這是一則要記進 `rebuild_info/pitfalls.md` 的事：**沿用前作的 `crt.c` 時，這一對（body `00044dc0` 與 thunk `0003dcb0`）不能跟著走進 crt。**

另外一則 pitfall 候選來自 `0003d8b2` 那份：前作的正典要求每個 public AIL 宣告都要帶 `#pragma aux ... modify [eax ebx ecx edx]`，在 `-3s` 下漏掉會安靜地毀掉呼叫端存在 EBX 的值。這條也還沒進 `pitfalls.md`。

## 沒有被推翻的：pool

這一輪 pool 一個都沒有改。進場時全庫 pool 只有 2 筆 medium，跑完 1,344 筆全部 high，分佈維持 crt 394／fdps 511／ail 439。

（run statistics 的 `byPool` 欄位是 fdps 23／ail 47／crt 60／unknown 3，那是**這一輪 133 筆記憶體記錄**的欄位值，而且被 placeholder 污染過，不是全庫分佈。要看全庫分佈只能掃判定檔。）

值得記的是 pool 的證據**升級**了不少，只是結論沒動。第一遍很多 pool 判定靠的是連結方向或形狀，這一輪換成了 dispatcher 或 library byte match：`0004c162` 從推論換成 `gtpid` 模組的三筆 OMF 記錄逐字相同，`00054922` 換成 `ioexit` 模組 offset 0x1a 起 91 個 byte 相同（順便把 boundary 用模組長度 0x75 算出來驗證），`0005506d`／`00055110`／`00055183` 換成 `MATH387S.LIB` 的 `ftos` 模組整份對齊。這種升級不會改任何結論，但它是「這個判定將來會不會被推翻」的差別。

## 落地階段：沒有東西要修，除了一個名稱碰撞

兩輪落地都很乾淨：

| | apply:rescan1 | apply:rescan2 |
| --- | --- | --- |
| 套用 | 128／128 | 2／2 |
| rename | 29 | 0 |
| boundary 變更 | 0 | 0 |
| body 變更 / function 增刪 | 0 | 0 |
| orphan range / undefined byte / error bookmark | 0 / 0 / 0 | 0 / 0 / 0 |
| naming gate | **1 個碰撞** | **1 個碰撞** |

`ApplyRereviewVerdicts` 兩輪都零 problem、沒有 NAME TAKEN。baseline gate 兩輪全清。程式有存檔。

唯一髒的是 naming gate：`__sys_init_387_emulator` 這個符號被 `0003d50a` 與 `000444a4` 兩個位址同時掛著。兩個落地 agent 都**沒有**去改它，也沒有加後綴——這是對的，決定哪個 body 才是那個符號是逐 function 的判斷，屬於複核 agent 不屬於轉錄階段。這條規則在這裡運作正常。

（另有一筆 `thunk_FUN_0004ec3c` 在 `0004f770` 與 `0004f79d` 重複，`AuditNames` 正確地把它歸到 AUTO-COLLISION 不計數：那是 Ghidra 自動產生的名字，兩個位址誰都沒有讓出任何判斷。）

## 那個碰撞：診斷錯過一次，然後留下一個沒解釋的洞

apply:rescan1 的 agent 說：「`000444a4` 在這輪清單裡，`0003d50a` 也在，所以**同一批的兩份判定選了同一個符號**。」

這句話是錯的。這一輪開跑前的 Ghidra 快照（`current.json`，08-16 20:52 匯出）顯示兩個位址**本來就都叫** `__sys_init_387_emulator`，而且 `sig_source` 是 `USER_DEFINED`——碰撞是票 14 留下來的，不是這一輪造的。apply:rescan2 的 agent 就判對了：「pre-existing, not caused by this round」。

真正沒解釋的是下一層。現在磁碟上的判定檔已經不衝突了：`0003d50a` 明確讓出符號、改回 `FUN_0003d50a`（理由是它只有五個 byte 的一條 `JMP`，不可雜湊、沒有自己的符號），`000444a4` 保留 `__sys_init_387_emulator`。掃過全部 1,344 個判定檔，`name.verdict` 只有 `thunk_FUN_0004ec3c` 重複，`__sys_init_387_emulator` 沒有第二個宣稱者。旁邊那組孿生 `0003d50f`／`00044629` 已經是正確形狀：thunk 保持 `FUN_0003d50f`，body 叫 `__sys_fini_387_emulator`。

**但 Ghidra 裡兩個名字還在。** rename 為什麼沒有落地，我沒有查出來。apply:rescan1 報了 29 個 rename、零 NAME TAKEN，所以不是被 `applyName` 的碰撞防護擋下（那會留下一行 problem）。剩下的可能性是 `0003d50a` 是 Ghidra 的 thunk function，`setName` 成一個 `FUN_` 形狀的名字時行為與預期不同——`0004f770`／`0004f79d` 那組 AUTO-COLLISION 是同一個機制的無害版本。這只是假設，**沒有驗證**。下一輪必須先確認 Ghidra 現況再說。

順帶一提，`ghidra_snapshot/` 是舊的（裡面 `0003d50a` 還掛著 `pool_binary_artifact`），這一輪結束沒有重匯。

## 交叉矛盾：agent 找到了，而且照規矩沒有動手

rescan 段最有價值的副產品是**跨判定檔的矛盾**——這種東西第一遍結構上看不到，因為那時沒人能讀別人的檔案。規則是「發現了就講，但永遠不准改寫別人的判定檔」，這一輪的 agent 全部遵守，所以這些矛盾現在是已知未解，不是已解：

- **`000378f0` 高信心地宣稱 `0x601c4` 那張表統一是 `void (*)(int)`**，但這張表的兩個 dispatcher 對參數個數看法不同（`FUN_0002e0c0` 推一個常數 0，`FUN_0001d990` 什麼都不推），而表裡好幾個 slot 是實打實的零參數。`0003a6b0` 就是被這個錯誤宣稱帶著憑空長出一個被忽略的 int 參數，這一輪把那個參數拿掉了，但 `000378f0` 本身沒動。
- **`0003cb01`（fdps）與 `0003cb93`（ail）是同一對 DPMI wrapper，卻被判進不同的 pool。** 兩份報告各自指出這件事，都說「其中一個大概是錯的」。前作把對應的 `fd2_dpmi_lock_size` 放在遊戲側的 `fd2common`。
- **零參數 void body 的 `__cdecl`／`__watcall` 標籤不一致**，至少 `00021100`／`00021200` 對 `00021140`／`00021180`／`00021240`、`0003b720`／`0003a3b0` 對 `0003b6b0`、`00047e30` 對 `0004b080`、`00054908` 對 `00054917`。零參數下兩者 emit 完全相同，所以沒有任何行為差別，但 `build_flags.md` 已經把整個 binary 的預設定死在 `-4s`，標籤該統一。這需要一次集中掃描，不是逐個 agent 各自猜。
- **`L$` 名字有兩種拼法**：8 份用字面 `L$N_`，13 份用序號（`L$1_`、`L$2_`）。而且中間那個 token 有兩種來源，一種是 object basename（`L$1_spve_...`），一種是 caller 的 public symbol（`L$1_vfscanf_...`）。前作的正典是 object basename，所以 `000431ef` 的 `L$N_vfscanf_ungetc` 與 `0004dca3` 的 `L$N_scanf_scan_int` 用錯了 token。加上前面 LPUBDEF 的發現，這一整堆名字多半根本不該用 `L$` 形式。
- **`0004bdda` 與 `0004bde9` 的 errno 常數讀錯了**：它們的判定把 0x0e 讀成 `EFAULT`、0x09 讀成 `EBADF`，但 Watcom 10.0a 的 `errno.h` 是 14 = `ERANGE`、9 = `EINVAL`（`EFAULT` 是 34、`EBADF` 是 4）。照 `CLIB3S.lst` 的模組符號，它們應該是 `__set_EDOM` 與 `__set_EINVAL`。

## 一個放錯地方的檔案

`CLIB3S.lst`（`wlib` 對已連結的 `CLIB3S.LIB` 產生的 listing，48 KB）躺在 repo 根目錄，未進版控。它是上一輪某個 agent 為了查模組符號現產的，然後這一輪好幾個 agent 直接把它當共用證據來源引用。CLAUDE.md 規定 scripts 的產物一律寫到 `workspace/{工作名稱}/`，這一份違規；而它同時又是有用的中間產物，所以不是刪掉就好——要嘛移進 `workspace/pool_rereview/`，要嘛把產生它的指令寫成 `tools/pool_rereview/` 下的一支腳本。

## 這一輪的淨結果

- 133 個判定被重讀，129 個判定檔被改寫，130 個經兩輪落地轉錄進 Ghidra。
- pool 零變更；boundary 零變更；body、function 增刪全部為零。改的是名字與簽章。
- 全庫五條軸的 confidence：pool 1,344 全 high、boundary 1,344 全 high、name 7 筆 medium、signature 14 筆 medium。
- 磁碟判準下還有 21 筆不過關（腳本自己報 3 筆，低報了）。
- naming gate 還髒著一筆，判定已經一致，缺的是一次轉錄。
- 313 個判定檔仍帶著 `open_question`——那些是刻意留給命名票與 emit 票的（參數意義、type code、struct 欄位），不是本票的未完成品。

三個腳本缺陷（假 pool placeholder、rescan 不填 `changed_axes`、單一 confidence 純量）都在報告層，沒有污染任何判定，但三個加起來讓這一輪的 run statistics 幾乎無法直接引用。下一輪跑之前先修。

## 收尾：上一節列的未解項目，逐條追完

上面那份「已知未解」清單寫完之後又跑了四輪小規模的 rescan，把其中能收的全部收掉。記在這裡的重點是**為什麼會漏**，不是補了什麼。

**rename 沒落地的原因查出來了，而且是我的 gate 錯，不是資料錯。** `0003d50a` 在 Ghidra 裡是一個 thunk function，`getName()` 回傳的是它跳去的 `000444a4` 的名字——thunk 沒有自己的符號。所以「兩個位址claim 同一個名字」從頭到尾就不存在，`AuditNames` 把繼承來的名字當成第二個宣稱者，誤報了整整四批。`setName` 之所以看起來沒生效，是因為根本沒有東西需要改。修法是在撞名比對裡跳過 `isThunk()` 的 function，順帶那個 `thunk_FUN_0004ec3c` 的 AUTO-COLLISION 也一起消失。教訓很直接：**gate 報出來的東西要先確認它問的問題是對的**，我盯著「怎麼讓 rename 生效」看了兩輪，方向從一開始就錯。

**DPMI 那六支的分歧，根因是我寫 prompt 時漏了專案鐵則。** CLAUDE.md 第一條工作步驟就是「動手前先查前作」，但 `rereview_ticket14_2.js` 的 pool 規則段裡從頭到尾沒有提到 `fd2-anatomy`。前三支之所以判對，是那三個 agent 自己想到去翻前作；後三支的 agent 沒想到，於是只剩連結方向這一條證據，而那條規則正好就是被前三支推翻的那條。把「前作已經回答過一部分，`rebuild_info/ail/calling_convention.md` 的 fd2common pool 是連結方向唯一的例外」寫進 prompt 之後重跑，三支立刻收斂到 `fdps`。**一個 agent 能不能查前作，不能靠它自己想起來。**

**`L$` 那一整堆名字，重點不是拼法而是它們本來就不該叫 `L$`。** 22 個裡有 13 個在補上 LPUBDEF 這條線索之後拿回了真名（`__exit`、`__set_EDOM`、`__set_EINVAL` 這類），剩下 14 個是記錄裡確實沒有命名該位移的。順帶暴露一個更大的東西：`00043310` 的 body 從 `_cstart_` 一路吃到收尾段，切開之後 `00043527` 就是 `CSTRT386.ASM` 裡的 `__exit`——**這是整張票唯一一處真正的邊界錯誤**，而它是被命名工作揪出來的，不是被邊界軸揪出來的。四個軸互相獨立這件事，在這裡反而變成互相搭救。

**新生的 function 沒有判定，是 gate 抓到的。** split 產生 `00043527` 之後，`AuditNames` 立刻報 `no_pool_tag = 1`。退休機制照設計運作：重跑匯出之後 `00043310`（body 縮小）與 `00043527`（全新）一起回到工作清單，判完才收工。這條路徑在整張票裡只走了一次，但它正是這張票的 workflow 與票 14 的差別所在。

**沒收的兩件，理由不同。** 零參數 `void` function 的 `__cdecl`／`__watcall` 標籤不一致仍在——那 14 個在 byte 層級無法區分，`build_flags.md` 已經把預設定死，emit 時照預設寫即可，在 Ghidra 裡統一標籤只是搬動一個沒有觀測依據的字串。`CLIB3S.lst` 移進了 `workspace/pool_rereview/`，但產生它的 `wlib` 指令還沒寫成腳本；它可重生，所以不擋收工。

最終狀態：1,345 個 function 全部有判定、判定與現況 body 雜湊逐一相符、兩道 gate 全乾淨。
