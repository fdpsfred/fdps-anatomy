# 2026-08-24 CRT 版本判定補強（票 16）

票 16 的起點是一個心裡有數的弱環節。`rebuild_info/build_flags.md` 寫著 Watcom C/C++ 10.0a，但那句話後面掛著括號：「10.0b 無法排除，兩者的相關產物 byte 相同」。四項證據裡真正逼近版本的只有兩段機械碼——`0x435f3` 的轉大寫常式和 `0x43657` 的「`__CHP`」——而這兩段在 10.0、10.0a、10.0b 三個版本裡是一模一樣的。

（那個 `__CHP` 是誤植，這次順手抓到的。`0x43657` 是 `int386x`，來自 `CLIB3S` 的 `intx386` 模組，公開符號就落在 offset 0；當初拿去比對的 22 byte 是它的開頭，是 `int386x(int, union REGS*, union REGS*, struct SREGS*)` 從堆疊搬四個引數的序列。真正的 `__CHP` 在 `0x3d4ec`，屬於 `MATH387S` 的 `fchop`，而且它連 10.5 之後都排除不掉——`fchop` 從 10.0 一路到 10.6a 沒有動過。所以舊結論那一列的證據力比它自己宣稱的還弱一點。）也就是說，撐住「10.0 家族」這個結論的證據很硬，撐住「10.0a 而不是 10.0b」的證據其實一個都沒有，只有「找不到反證」。

票 14 之後 Ghidra 裡累積了 395 個確認的 `pool_crt` function。每一個都是一個新的判別點。這張票就是把這 395 個判別點全部用掉。

## 先走編譯器那條路，然後撞牆

第一個念頭不是比函式庫，是比編譯器。理由很直接：函式庫版本等於編譯器版本這件事本身是個推論，兩者是分開的檔案，理論上建置機器可以裝著不成對的組合。票 16 的第三條 checkbox 就是要驗證或明確標記這個推論。順手驗證的話，說不定還能得到一個獨立於函式庫的第二根槓桿。

`tools/crt_version/compiler_diff.py` 拿 `WATCOM_10.0a_infobase\SAMPLES\CLIBEXAM` 的 338 個 C 檔當語料，在 DOSBox-X 裡用三個版本各自的 DOS 版 `wcc386`、同一組已定案的旗標（`-bt=dos4g -mf -zq -4s -fpi -s -ot -od`）各編一遍，再用同一套「忽略模組自己的 FIXUPP 欄位」的規則比對產出的 `_TEXT`。結果分成兩半：

- 10.0 對 10.0a：336 個編成功，312 個 byte 相同，**22 個不同**。差異集中在 `isalnum`/`isdigit`/`iscntrl` 這類 `ctype` 巨集、`getc`/`putc`/`ungetc` 這類 stdio 巨集，以及 `chainint`/`getvect`/`setvect`/`va_arg`。
- 10.0a 對 10.0b：334 個可比，**0 個不同**。

第二行就是牆。10.0a 和 10.0b 的編譯器在整個 338 檔語料上產生完全相同的碼。這條路對「10.0a vs 10.0b」永遠不會有答案，不是這次語料選得不好，是根本沒有東西可找。

不過在放棄之前先確認了第一行不是假的。那 22 個檔清一色是巨集重的檔案，很容易懷疑差異其實出在標頭檔而不是編譯器——如果只是 `ctype.h` 改了巨集展開，那說明不了編譯器的事。所以寫了 `tools/crt_version/probes/bittest.c` 這個單檔探針，把標頭統一釘在 10.0a 的版本上，再用三個編譯器編（`report_h10.0a.json`）。10.0 出來 225 byte，10.0a 和 10.0b 都是 257 byte。標頭相同、產出仍然不同，差異確實在編譯器裡：對記憶體中某個 byte 做位元測試時，10.0 會折成 `TEST byte ptr [mem],imm8`，10.0a 和 10.0b 則先把 byte 載進暫存器再測暫存器。

這個副產品後來變成一個獨立的旁證。`CountCodeShapes.java` 在 FDPS.LE 裡逐條指令數這兩種形狀，只算遊戲 pool（AIL 和 CRT 是別人在別的時間用別的設定編的，形狀不能算數）：遊戲碼裡 `load_then_test_reg` 有 38 個，`test_mem8_imm` 一個都沒有。編譯器不是 10.0，這件事不靠任何函式庫比對就成立。但它同樣切不開 10.0a 和 10.0b——因為那兩版的編譯器本來就一樣。

結論寫進票裡：函式庫是唯一的槓桿，而且會永遠是。

## 建 sweep：能踩的坑幾乎都是別人踩過的

`sweep_versions.py` 做的事說起來很簡單：把每個 CRT function 的 body 拿去在十二個安裝的每一個函式庫模組裡找一個「放得下」的位置。麻煩全在細節。

抽 `.lib` 一律用 `wlib -q -x`，不自己寫 reader——這是票 14 用一整天換來的結論，Easy OMF-386 的 quirky record 會把任何直觀的 record walk 打斷，前作的知識庫早就寫著「用 `wlib` 別自己寫」。抽出來的 `.obj` 還要先過票 14 的 record patcher，理由同上。

`omf_image.py` 處理的是兩件擋在「單純比 byte」前面的事：程式碼在 `.obj` 裡是分散在多個 LEDATA record 的，檔案裡並不連續，得先重組成 segment image；而每個 linker 會 patch 的欄位在 `.obj` 裡還留著佔位值，即使程式碼完全相同，那些 byte 也一定不一樣。所以每個 segment 除了 image 還產一份 mask，把 FIXUPP record 宣告過的每個 byte 標起來。比對時**只**忽略這些 byte，其他地方一有不等就是真的證據。

FDPS 這一側同理。`DumpCrtBodies.java` 標記可重定位位置時用了兩道：一道從指令自己的 reference 來，一道用「掃描任何落在 image 範圍內的 4-byte little-endian 值」來兜底，怕的是 Ghidra 漏記某個 reference。刻意選擇寧可過度遮罩：多遮只會縮短可比的長度，少遮會**偽造出一個版本差異**。這條取捨在後面救了不少命。

## 三種讓 sweep 講出瘋話的方式

sweep 跑出來的第一版結果不能直接用。三種毛病，性質完全不同。

**一、錨點搜尋的偽陽性。** sweep 的規則是「body 的最長未遮罩片段當錨點，在模組裡找所有出現位置，逐一驗證」。錨點下限訂在 4 byte（再短候選會爆到幾千個），但 4 byte 對某些 function 還是太短。最典型的是 `IF@TAN`：它自己只有 5 個 byte，`d9 f2 dd d8 c3`（FPTAN / FSTP ST0 / RET），零重定位。sweep 說 9.5 全系列都相容。實際去看，9.5 的 `trig387` `_TEXT` 是 431 byte、完全不同的佈局，那 5 個 byte 命中在 offset 316，是一個從 307 開始的更長常式的落尾——前面掛著 `CMP byte [fpu_mode],3 / JNE +5` 這道 FPU 模式閘。sweep 的「任何模組任何位置」規則沒辦法分辨「這是函式的進入點」和「這是別人的尾巴」。

**二、負面論證太弱。** `IF@TAN` 第一次判定給了 medium，理由寫的是「任何版本的任何模組裡都找不到這 5 個 byte」。對 5 個 byte 來說這個論證幾乎沒有價值——找不到可能只是沒找對地方。rescan 才把它改成正面論證：整段 `trig387` `_TEXT` 在 image 裡是連續的（0x00042c30 起，2π 常數、`IF@COS`、`IF@SIN`、範圍縮減 helper、`cos`、`sin`、`IF@TAN`、`tan`），把 0x42c30–0x42cb7 這 136 byte 整段拿去和各版本重組出來的 `trig387` `_TEXT` 對，10.0/10.0a/10.0a_infobase 的 `_TEXT` 剛好也是 136 byte，零個未遮罩差異；10.0b 的是 152 byte，前 112 byte 一模一樣，然後在正好這個 offset 換成一個 21 byte 的帶 guard 版本（`TEST byte [flag],1` / `JNZ` / FPTAN / `JMP` / `CALL` 軟體 fallback / FSTP / RET）。21 byte 的 body 在任何擺法下都不可能是 image 裡那 5 byte 的函式，而且下游佈局也跟著證實：image 的 `tan` 在 `_TEXT+117`、call 位移是 `e8 f2 ff ff ff`，10.0b 的在 `_TEXT+133`、位移 `e8 e2 ff ff ff`，剛好差 16 byte。同一次改判裡順手把 9.5 從 compatible 移掉。confidence 升到 high。

這是本票裡最值得記的一次翻案：同一個結論（10.0b 出局），第一次的理由撐不住，第二次換成定位比對才撐得住。如果沒 rescan，10.0b 的排除就只剩 `strtod` 一根獨木。

**三、模組不存在被當成排除。** 這個最陰險。aggregate 跑出來，raw intersection 只剩一個標籤：`10.0a_infobase`。而 `10.0a` 被五個 verdict 排除掉了——`FUN_00043612`、`__CHK`、`__GRO`、`__STK`、`__STKOVERFLOW`，全部是 `CLIB3S` 的 `stk386` 模組，可比長度 4/12/3/22/4 byte。字面上讀，這是「兩個 10.0a 安裝互相矛盾」。

查了 `lib_index.json` 和磁碟上的檔案：

- `WATCOM_10.0a\lib386\dos\clib3s.lib` — 190,464 byte，**393** 個模組，**沒有 `stk386`**
- `WATCOM_10.0a_infobase\lib386\dos\clib3s.lib` — 190,976 byte，**394** 個模組，有 `stk386`
- 兩邊共有的 393 個模組，SHA-256 **零個**不同
- 其餘五個 library（EMU387 2 個、GRAPH 257 個、MATH387S 78 個、兩個 STARTUP 各 1 個）逐模組雜湊完全相同

所以這台機器上的 `WATCOM_10.0a` 安裝，`clib3s.lib` 是個短了一個模組的壞檔。sweep 在那個版本下根本沒有東西可比，記成 miss，aggregator 把 miss 讀成排除。同一份 `stk386` 物件在 10.0a_infobase 和 10.0b 之間 byte 相同（SHA `4bc3584c…`），從 9.5 到 10.6a 每個真實版本都出這個 stub。

修正方式是把兩個安裝讀成同一個 release，不是去改 aggregator 的規則。`__CHK` 的 verdict 自己就寫了這句話：這五個 function 不得用來把 10.0a 和任何東西分開。機器上要做的事是把 `WATCOM_10.0a\lib386\dos\clib3s.lib` 從 infobase 安裝複製回來——但這是環境的修復，不影響結論。

值得停下來想一秒的是這件事的教訓：sweep 分不出「這個版本的這段碼不一樣」和「這個版本這裡沒東西可比」，兩者都變成 `match: false`。這次是因為五個 exclusion 全擠在同一個模組才被抓到。如果壞掉的是分散在不同模組的十個 function，可能就直接把一個版本冤枉掉了。

## 真正切開 10.0b 的兩個 function

排除 10.0b 全靠兩個，兩個都在 `MATH387S`，而且都不薄。

`strtod`（0x00055443）是主力。491 byte 連續 body，48 byte 被 FIXUPP 遮掉，443 byte 可比。10.0b **確實有** `strtod` 模組（這不是上面那個缺模組的狀況），`_TEXT` 是 492 byte 對 image 的 491。把所有可能的對齊位置都跑一遍，10.0、10.0a、10.0a_infobase 都是 mismatch 0，10.0b 最好的情況是 443 個裡面 **362 個不合**。分歧從 offset 7 開始：10.0b 是 `SUB ESP,0x38` 和 `[EBP-0x18]`，image 是 `SUB ESP,0x34` 和 `[EBP-0x14]`——10.0b 重編了 `strtod`，frame 大了 4 byte，所有 local 位移全部平移。不是差一點點。

`IF@TAN`（0x00042ca0）是上面講過的那個。自己只有 5 byte，但靠整段 136 byte 模組的定位比對成立。

兩個分別在不同的判別基礎上（一個是長 body 的位元組比對，一個是整段模組的位置比對），任何一個單獨拿掉，10.0b 的排除都還在。

## 排除 10.0 的七個

| 位址 | 名稱 | 模組 | 可比 byte |
| --- | --- | --- | ---: |
| 0x0004c423 | `__prtf` | `prtf` | 649 |
| 0x0005254b | `__isindst` | `timeutil` | 595 |
| 0x0004d2da | `__scnf` | `scnf` | 396 |
| 0x00043f3e | `__ReAllocDPMIBlock` | `grownear` | 253 |
| 0x0003d383 | `_nmalloc` | `nmalloc` | 173 |
| 0x0004352a | `__do_exit_with_msg__` | `cstrt386` | 50 |
| 0x00043298 | `_cstart_` | `cstrt386` | 2 |

`__prtf` 最硬：在模組自己的 `__prtf` public offset 0 上做定位比對，對 10.0a 和 10.0b mismatch 0，對 10.0 是 649 裡 606 個不合。結構差異在 image 裡直接看得到——10.0 的 `__prtf` 是 687 byte、`SUB ESP,0x48`、輸出 callback 直接穿過引數呼叫（`FF 54 24 74`）；10.0a/10.0b 的是 695 byte、`SUB ESP,0x4c`、callback 先複製到 local 再透過它呼叫（`FF 54 24 4c`），0x0004c423 做的是後者。

`__isindst` 是另一個模組來的獨立佐證，而且它特別能回答「會不會是模組沒被解析到」這個質疑：它在 `timeutil` 的三個 sibling（`__leapyear`、`calc_yday`、`check_order`）在 10.0 的 offset 0、57、298 全部 mismatch 0，證明 10.0 的 `timeutil` 確實被解析也被掃過了——只是 10.0 的 `__isindst` 佔 721 byte，image 是 728，這個常式在 10.0a 長了 7 byte。

2 byte 的 `_cstart_` 是真的，但單獨什麼都證明不了，結論沒有靠它。

## 完全沒有資訊的 14 個

395 個 verdict 裡有 14 個 basis 是 `too_short`、power 是 `none`，一個版本都區分不了：

`FUN_00010000`(3)、`delay`(5/可比 1)、`__sys_init_387_emulator`(5/1)、`FUN_0003d50f`(5/1)、`L$1_rand_seed_ptr`(6/2)、`FUN_00042e0e`(1)、`FUN_00044789`(1)、`getpid`(6/2)、兩個 `thunk_FUN_0004ec3c`(5/1)、`FUN_00051b7d`(1)、`tryOSTimeZone`(1)、`__get_errno_ptr`(6/2)、`_dos_findclose`(3)。

清一色是 1 到 6 byte 的 thunk 和取址常式，扣掉重定位之後往往只剩 1 到 3 個 byte。它們不是判錯，是本來就沒有可比的東西。

這個數字曾經更大。`__STKOVERFLOW` 的 verdict 裡留著一句當時的觀察：「aggregate 有 29 個 too_short、28 個 power none」。差額是 rescan 一個一個救回來的——`__STKOVERFLOW` 自己就是靠同模組裡一個已釘住的鄰居固定了模組載入位址，才能在已知的 public offset 上做定位比對，而不是靠錨點搜尋。這也是它在 verdict 裡留下的建議：sweep 應該有一個「錨點為空時退回定位比對」的機制，那是工具的事，不是任何單一 verdict 的事。這條建議目前沒有實作。

## 這次呼叫本身跑得很乾淨

進來時 395 個 packet 裡已經有 394 個有可用的 verdict，這一輪只判了 1 個。Gate 第一輪檢查 1 個、通過；rescan 掃了 26 個、通過。沒有因為預算延後的項目、沒有未完成、沒有中途停止。

rescan 只改動一個 verdict：`00054908` `__full_io_exit`。而且改的只有 confidence 欄，medium → high，版本結論（十二個版本全相容、排除零個、basis `ambiguous_match`、power `family_only`）一個字沒動。原本的猶豫是模組歸屬只靠 offset 0 的符號擺放，不是靠倖存的那 7 個 byte。兩件事解掉了它：一是這個 function 自己的兩個重定位目標都被別的 verdict 釘死了（0x0005490a 的 CALL 到 0x00054922 = `docloseall`，同樣在 `CLIB3S!ioexit`，75 byte high；尾端 JMP 到 0x0004bef1 = `__purgefp`，`CLIB3S!allocfp`，18 byte high），被呼叫者就住在這個 function 的 offset-0 符號指名的那個模組裡，模組歸屬因此有了結構性的佐證。二是 aggregate 顯示它是整個 pool 裡唯一一個十二版全相容卻只給 medium 的 `ambiguous_match`；結構完全相同的 7-byte thunk `fcloseall`(0x00054917) 和 `flushall`(0x0005497d) 都是 high，`fcloseall` 的 module 欄甚至是空的。既然什麼都沒排除，byte 稀少影響的是 power（已經記成 `family_only`），不是可靠度；一個過寬的 compatible set 只會沒能約束交集，不可能污染交集。

順帶一提，整個 pool 累積下來有 127 個 verdict 帶著 `_supersedes`、5 個帶著 `_reread`——rescan 在這張票裡不是走過場。

## 結論與還沒做的事

395 個 verdict，381 個有資訊，392 個 high。Raw intersection 是單一標籤 `10.0a_infobase`，只取 high confidence 得到完全相同的集合。把兩個 10.0a 安裝讀成一個 release，交集就是恰好一個版本：**Watcom C/C++ 10.0a**。

10.5 之後被 105–111 個 verdict 排除，9.5 家族被 270–272 個排除，家族外的邊界是超定的，需要小心讀的只有家族內的切分。

對照 `rebuild_info/build_flags.md` 的既有結論：版本一致，但**括號裡那句話被推翻了**。「10.0b 無法排除，兩者的相關產物 byte 相同」不成立——`MATH387S.LIB` 的 `strtod` 和 `trig387` 在 10.0b 都改過，FDPS.LE 帶的是 10.0a 的那一份。而既有結論引用的那兩段短 byte 串（`strupr`、以及那個其實是 `int386x` 的「`__CHP`」）是 family-only 證據，本來就不可能切開這個家族；切開它需要一個真的在 10.0a 到 10.0b 之間改過的模組，`strtod` 就是。

收尾時補的三件事，都是這次跑出來的副產品：

`sweep_versions.py` 原本把每版的命中清單截到 8 筆，而 `hit_count` 可能是 17 到 21——封包裡沒有任何欄位說它被截過。`0003d375` 的 rescan 就是踩到這個：`nmalloc.obj` 是十二個安裝裡唯一定義公開符號 `malloc` 的模組，卻剛好落在被砍掉的那一截，第一次判定因此只能猜模組。現在加了 `hits_truncated`，並且無論截不截都完整列出 `hit_modules` 與 `hit_publics`。

票 14 的 `tools/pool_triage/fid/extract_libs.py` 也還指著壞掉的那份 10.0a，改成 infobase 那份。回頭確認過這件事沒有污染票 14 的結論：`FidQuery` 是三個版本的 fidb 各查一次，`stk386` 在 10.0 和 10.0b 的 fidb 裡都在，所以 `__CHK`／`__STK`／`__GRO`／`__STKOVERFLOW` 當時仍然被正確命名。

`__STKOVERFLOW` 的 verdict 建議 sweep 加一個「錨點為空時退回定位比對」的機制，這條沒有實作，但代價清點過了：剩下的 14 個裡每一個都只有 1 到 3 個可比 byte，其中 4 個的模組已經由鄰居釘出來（`delay386`、`386inite`、`save8087`）且確認跨版本未改。1 到 3 個 byte 在任何一版都對得上，通則化只會多幾筆「相容」，不可能多排除任何版本，所以它改變不了結論。這一段記在 `tools/crt_version/_index.md`。

機器層面還剩一件事沒做：`WATCOM_10.0a\lib386\dos\clib3s.lib` 仍然是壞的。兩份安裝都留著、由判定階段自己比出哪份完整，比默默用其中一份安全，所以沒有動它；處置寫在 `rebuild_info/pitfalls.md`。
