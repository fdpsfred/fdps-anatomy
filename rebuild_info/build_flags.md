# 建置旗標與預設 calling convention

`FDPS.LE` 是用哪一套工具鏈、哪一組編譯與連結旗標產生的。本檔擁有旗標組本身與每一項的判定依據；位址空間的分段與邊界由 [`program_info/memory_layout.md`](../program_info/memory_layout.md) 擁有，本檔只引用。

判定的是**整份 binary 的預設值**。個別 function 的 calling convention 有例外，處理方式見本檔最後一節。

## 工具鏈

**Watcom C/C++ 10.0a。** 10.0 家族的另外兩個發行版同樣被排除，9.5 全系列與 10.5 之後也是。

判定的方式是把映像檔裡 395 個 `crt` function 逐一與**每個候選發行版的每個程式庫模組**比對。候選是手上 11 個發行版（9.5、9.5a、9.5b、9.5c、10.0、10.0a、10.0b、10.5、10.5a、10.6、10.6a）的 DOS 32-bit 堆疊慣例執行期，`CLIB3S`、`MATH387S`、`EMU387`、`GRAPH`、`CSTRTX3S` 拆成 OMF module 共 9,309 個。比對只遮掉該模組自己的 FIXUPP 記錄宣告連結器會改寫的 byte，其餘每個 byte 都要相等；遮多了只會縮短可比長度，遮少了才會捏造出版本差異。

| | 數量 |
| --- | ---: |
| 逐一判定的 `crt` function | 395 |
| 問得出相容集合的 | 381 |
| body 短到沒有可比 byte 的 | 14 |
| 其中分辨得出發行版的 | 274 |

**381 個相容集合的交集是單一發行版：10.0a。** 只取 392 個高信心判定，交集完全相同。

| 發行版 | 相容的 function | 排除它的 function |
| --- | ---: | ---: |
| 9.5／9.5a | 109 | 272 |
| 9.5b／9.5c | 111 | 270 |
| 10.0 | 374 | 7 |
| **10.0a** | **381** | **0** |
| 10.0b | 379 | 2 |
| 10.5 | 276 | 105 |
| 10.5a | 273 | 108 |
| 10.6／10.6a | 270 | 111 |

本機有兩份 10.0a 的安裝，上表這一列取的是完整的那份。另一份的 `clib3s.lib` 少了 `stk386` 模組，`00043612`、`0004361a`（`__CHK`）、`0004362a`（`__GRO`）、`0004362d`（`__STK`）、`0004364b`（`__STKOVERFLOW`）五個堆疊檢查 stub 因此在那份安裝上無從比對，被記成排除。那是檔案殘缺不是版本差異——五個 stub 在完整的 10.0a 與在 10.0b 裡 byte 相同（`stk386` 的 SHA-256 兩邊一致），**不能拿來分辨任何版本**。安裝本身的處置見 [`pitfalls.md`](pitfalls.md)。

### 排除 10.0 的七個 function

| 位址 | 名稱 | 程式庫模組 | 可比 byte |
| --- | --- | --- | ---: |
| `0004c423` | `__prtf` | `CLIB3S` `prtf` | 649 |
| `0005254b` | `__isindst` | `CLIB3S` `timeutil` | 595 |
| `0004d2da` | `__scnf` | `CLIB3S` `scnf` | 396 |
| `00043f3e` | `__ReAllocDPMIBlock` | `CLIB3S` `grownear` | 253 |
| `0003d383` | `_nmalloc` | `CLIB3S` `nmalloc` | 173 |
| `0004352a` | `__do_exit_with_msg__` | `CLIB3S` `cstrt386` | 50 |
| `00043298` | `_cstart_` | `CLIB3S` `cstrt386` | 2 |

`__prtf` 最強：以模組自己的 `__prtf` 公開位移 0 對位比較，對 10.0a／10.0b 零不符，對 10.0 是 649 個可比 byte 裡 **606 個不符**。差異在映像檔本身看得見——10.0 的 `__prtf` 是 687 byte，`SUB ESP,0x48`，輸出 callback 直接透過引數呼叫（`FF 54 24 74`）；10.0a／10.0b 是 695 byte，`SUB ESP,0x4c`，callback 先複製到區域變數再透過它呼叫（`FF 54 24 4c`），`0004c423` 是後者。

`__isindst` 是另一個模組給的獨立佐證：同在 `timeutil` 的 `__leapyear`、`calc_yday`、`check_order` 在 10.0 的該模組裡分別於位移 0、57、298 零不符地對上，所以 10.0 的 `timeutil` 確實被解析也被掃過；只有 `__isindst` 自己在 10.0 佔 721 byte 而映像檔是 728——這支常式在 10.0a 長了 7 byte。

`_cstart_` 那 2 個 byte 是真的，但單獨不說明什麼，結論不靠它。

### 排除 10.0b 的兩個 function

| 位址 | 名稱 | 程式庫模組 | 可比 byte |
| --- | --- | --- | ---: |
| `00055443` | `strtod` | `MATH387S` `strtod` | 443 |
| `00042ca0` | `IF@TAN` | `MATH387S` `trig387` | 5 |

`strtod` 不薄。10.0b **有** `strtod` 模組（不是上面那種缺模組的情形），`_TEXT` 是 492 byte 對映像檔的 491。逐一試過每個可能的對位，對 10.0、10.0a 零不符，對 10.0b 最好的結果是 443 個可比 byte 裡 **362 個不符**。差異從位移 7 開始：10.0b 是 `SUB ESP,0x38` 與 `[EBP-0x18]`，映像檔是 `SUB ESP,0x34` 與 `[EBP-0x14]`——10.0b 把 `strtod` 重編成大 4 byte 的框架，其後每個區域變數跟著位移。

`IF@TAN` 只有 5 個 byte（`d9 f2 dd d8 c3`，FPTAN／FSTP ST0／RET，無重定位），單獨拿等於沒有。撐住它的是整個模組段：`trig387` 的 `_TEXT` 在映像檔裡連續落在 `00042c30`，`00042c30`–`00042cb7` 這 136 byte 與 10.0／10.0a 重組出來的 `MATH387S` `trig387` `_TEXT`（本身也正好 136 byte）逐 byte 相同。10.0b 的 `trig387` 是 152 byte：前 112 byte 相同，接著在正是這個位移上放的是 21 byte 的守衛版 `IF@TAN`（`TEST byte [flag],1`／`JNZ`／FPTAN／`JMP`／`CALL` 軟體回退／FSTP／RET），而不是 5 byte 的裸核心。21 byte 的 body 在任何對位下都不可能是映像檔那支 5 byte 的 function，下游佈局也跟著對上：映像檔的 `tan` 起於 `_TEXT+117`、呼叫位移 `e8 f2 ff ff ff`，10.0b 的起於 `_TEXT+133`、`e8 e2 ff ff ff`，差的正是長版 `IF@TAN` 撐開的那 16 byte。

兩者互相獨立：兩個不同程式庫的模組、兩處不同的程式碼改動，指向同一個方向。少掉任何一個，10.0b 的排除都還站得住。

### 上下界

上下界都不是接近的判定。10.5 被 105 個 function 排除、10.5a 108 個、10.6 與 10.6a 各 111 個，其中份量最重的是 `00042a41` 的 `fwrite`（439 個可比 byte）、`0004270d` 的 `fread`（365）與 `0004252c` 的 `__doopen`（235）。9.5 家族被 270–272 個排除。收斂到 10.0 家族這一步是被過度決定的，需要細讀的只有家族**內部**的切分。

### 程式庫比對以外的旁證

| 證據 | 內容 |
| --- | --- |
| CRT 的版權字串 | `0003fa49`：`WATCOM C/C++32 Run-Time system. (c) Copyright by WATCOM International Corp. 1988-1994.` 年份上界 1994 與 10.0 家族一致 |
| 隨遊戲附的 `DOS4GW.EXE` | 265,420 byte，SHA-256 與 10.0a／10.0b 的 `BIN\DOS4GW.EXE` **完全相同**。9.5c 大小相同但雜湊不同，10.5 之後是 265,396 byte |
| `FDPS.EXE` 的 MZ stub | 10,832 byte。用 10.0a 的 `wlink system dos4g` 連結任意程式，產出的 stub 與 `FDPS.EXE` 的前 10,832 byte **零 byte 差異** |

這三項全部只到家族層級。`000435f3` 的 `strupr`（`CLIB3S` `strupr`，31 個可比 byte）與 `00043657` 的 `int386x`（`CLIB3S` `intx386`，31 個）也一樣——它們在 10.0、10.0a、10.0b 的 `CLIB3S.LIB` 裡 byte 相同，能排除 9.5 與 10.5 之後，但本來就不可能切開 10.0 家族。切開家族要靠真的在版本之間改過的模組，那就是上面兩張表。

`__CHP` 不在 `00043657`，而在 `0003d4ec`（`MATH387S` `fchop`，29 個可比 byte），而且它連 10.5 之後都排除不掉——`fchop` 從 10.0 到 10.6a 沒有動過。

`00042c3a` 的 `FCOS`／`FSIN` 包裝常式命中 `MATH387x.LIB`（非 `MATH3x`），`0004ec3c` 起的 80x87 模擬器命中 `EMU387.LIB`。

### 編譯器的版本沒有跟著程式庫一起被量到

上面量到的是**執行期程式庫**的發行版。編譯器是同一份安裝裡的另一個檔案，「程式庫是 10.0a，所以編譯器也是 10.0a」是推論不是量測。這個推論有一半量得到、一半量不到。

**量得到的一半：編譯器不是 10.0。** 10.0 與 10.0a 的 `wcc386` 對「測記憶體裡一個 byte 的某個位元」發出的碼形狀不同：10.0 把運算元收進 `TEST byte ptr [mem],imm8`，10.0a 之後先把 byte 載進暫存器再測暫存器。`FDPS.LE` 的遊戲段有 38 處後者、**0 處**前者。（前者在 `crt` 段有 75 處、`ail` 段有 17 處，但那兩段是別人在別的時間用別的設定編的，對原版的建置環境不算證據。）

**量不到的一半：10.0a 與 10.0b 的 `wcc386` 產生完全相同的碼。** 以舊的 `-s -ot -od` 把同一份 338 個檔的語料（Watcom 隨附的 `CLIBEXAM` 範例）分別用三個發行版的 DOS 版 `wcc386` 編過，比對組出來的 `_TEXT` 影像並忽略各自的 fixup 欄位（每個發行版都編出 336 個 `.obj`，兩兩可比的是 334 個）：

| 比對 | 相同 | 不同 |
| --- | ---: | ---: |
| 10.0 vs 10.0a | 312 | 22 |
| 10.0 vs 10.0b | 312 | 22 |
| **10.0a vs 10.0b** | **334** | **0** |

10.0 那 22 個差異集中在 `ctype` 家族與 `getc`／`putc`／`ungetc`，光憑上表無法排除它們只是標頭檔裡的 inline 巨集換了。把三個發行版**全部釘在 10.0a 的標頭檔**上重編位元測試探針，10.0 仍然與 10.0a 不同（225 對 257 byte），10.0a 與 10.0b 仍然相同——所以 10.0 的差異在程式碼產生器，而 10.0a 與 10.0b 的相同也不是共用標頭檔造成的。

**從 binary 判不出編譯器是 10.0a 還是 10.0b，而且判不出來不花代價。** 兩者在這組旗標下的產出一模一樣（`-oe=25 -d2` 之下沒有重量過），就算原版真的是 10.0b 的編譯器配 10.0a 的程式庫，用 10.0a 重建也會得到同一份機械碼。有代價的是另一邊：**程式庫必須是 10.0a 的**，10.0b 的 `MATH387S.LIB` 換過 `strtod` 與 `IF@TAN`，見 [`pitfalls.md`](pitfalls.md)。

## 編譯旗標

```
wcc386 -bt=dos4g -mf -4s -fpi -s -ot -oe=25 -d2
```

這是遊戲段（CD 模組以外）的旗標組；CD 模組另有一組，見下節。

**`-ot` 必須在 `-d2` 之前。** `wcc386` 由左而右處理選項：`-ot` 先設定「以速度為優先」這個偏好，`-d2` 再關掉最佳化器但不會清掉那個偏好。`-ot` 擺到 `-d2` 之後（`-s -d2 -oe=25 -ot` 或 `-s -oe=25 -d2 -ot`）會把最佳化器重新打開：42 支真實 function 的逐 byte 對照從 17 支完全相同掉到 0 支，`verify_flags.py` 的 15 項特徵只剩 9 項（序幕、不用 `LEAVE` 的收尾、`MOVSX`、兩項 switch 表、區域變數來回堆疊全錯）。`-oe` 與 `-d2` 之間的順序不影響結果。

**`-d2` 與 `-od` 都關掉最佳化，但產出不同，原版是 `-d2`。** `-d2` 是「帶完整除錯資訊編譯」，它關掉最佳化的方式與 `-od` 不同：程式設計師宣告的變數（區域變數與參數）永遠從它的堆疊槽重新載入暫存器再用，編譯器自己配的暫存槽則照一般方式直接當運算元。三處可以直接在映像裡看到的差異全部站在 `-d2` 這邊：

| 形狀 | `-od` | `-d2` | 原版 |
| --- | --- | --- | --- |
| 記憶體運算元當呼叫引數 | 一律 `PUSH dword ptr [EBP-n]` | 宣告的變數經 `MOV EAX,…` / `PUSH EAX`，展開出來的暫存槽直接推 | 2,587 處全部是 `-d2` 的樣子，見下文「引數推送形式」 |
| 經函式表間接呼叫、帶記憶體引數 | 以 EAX 做索引、引數直接推 | 以 EDX 做索引、引數經 EAX 中轉 | 7 處全部是 `-d2` 的樣子（`00012a33`、`00012aea`、`00012bd6`、`00014d28`、`00015839`、`000188d8`、`0001dcae`）；唯一帶立即值引數的 `0002e140` 兩者相同 |
| 以真實原始碼逐 byte 對照（見「重現方式」） | 42 支中 6 支相同 | 42 支中 17 支相同 | `-od` 對上的 6 支在 `-d2` 下全部也對上 |

`-d2` 另外把 `$$SYMBOLS`／`$$TYPES` 段寫進目的檔，連結時不帶 `DEBUG` 指令就被 wlink 丟掉，執行檔裡不留痕跡。`-d2` 之下區域變數的堆疊槽位順序跟著宣告順序，所以宣告順序不同會讓整支 function 的槽位位移——這影響逐 byte 對照，不影響行為。`-d1` 不是候選：它不關最佳化。

每一項的判定依據如下。「原版的樣子」欄位是 `FDPS.LE` 的實際觀察，「另一種選擇會變成」是用同一支編譯器實測其他旗標的產出。

| 旗標 | 原版的樣子 | 另一種選擇會變成 |
| --- | --- | --- |
| `-bt=dos4g` | CRT 走 `__x386_init` 這條 DOS/4G 啟動路徑，執行檔是 LE 容器配 DOS/4G stub。容器格式其實由 wlink 的 `system` 決定，這個旗標決定的是預定義巨集與 header 搜尋路徑 | 換成別的 build target 會拉到另一套 header 與啟動碼 |
| `-mf`（flat） | 具名 const 物件與區域陣列的初值影像放在 object 1（`0x146d2`、`0x2b27a`、`0x31037` 等夾在遊戲函式之間）；字串字面值與浮點常數放在 object 2 的 `CONST`；複製初值到堆疊前**不重載 ES** | `-ms` 會把 const 一起放進 DGROUP，且每次複製前多兩條 `mov ax,ss` / `mov es,ax` |
| `-4s`（486，堆疊呼叫慣例） | 序幕固定 `53 56 57 55 89 e5`（推 EBX/ESI/EDI/EBP 後建 EBP 框架），第一個引數在 `[ebp+0x14]`；收尾用 `mov esp,ebp` / `pop ebp`，全 binary 遊戲段 **0 個 `LEAVE`**；16-bit 載入保留 `MOVSX`（208 處） | `-3s` 收尾用 `LEAVE`；`-5s` 把每個 `movsx eax,word ptr X` 換成 `mov eax,dword ptr X-2` + `sar eax,0x10`（遊戲段 0 處）；`-4r`／`-3r` 等 register 慣例不會無條件推四個暫存器 |
| `-fpi`（內嵌 x87，含模擬） | 遊戲段有 70 條內嵌 x87 指令，且映像檔內含 `EMU387.LIB` 的 80x87 模擬器 | `-fpc` 完全不產 x87，改呼叫 `__I4FD`／`__FDM` 等；`-fpi87` 只差在不發出 `__init_387_emulator` 這個外部參照，**實測即使在 `.lnk` 明列 `emu387.lib`，`-fpi87` 產出的映像檔裡也沒有模擬器**——wlink 只抽出解得掉未定義符號的 lib 成員 |
| `-s`（移除堆疊檢查） | 標準框架的遊戲 function **沒有任何一個**呼叫 `__CHK`（`0x4361a`）。**但 CD 模組例外**，見下節——`__CHK` 的 34 個呼叫端裡有 32 個是 `fdps_cd*` | 不加 `-s` 時每個有框架的 function 都會被插入 `push <框架大小>` / `call __CHK` |
| `-ot`（以速度為優先） | 位址計算的索引縮放編成 `lea reg,[reg*N + 0]`：遊戲段 304 處，程式庫段 41 處。`shl reg,2` 只出現在除法常數展開之類的算術情境（37 處） | 不加 `-ot` 時位址縮放也用 `shl reg,N`；`-os`（以空間為優先）同樣是 `shl` |
| `-d2`（完整除錯資訊，連帶關閉最佳化） | 每個區域變數都寫回堆疊再讀出；switch 的跳躍表放在序幕之後、以 `jmp short` 跳過，分派拆成 `mov` + 縮放 + `jmp cs:[reg+表]` 兩三條指令；宣告的變數當引數時經 EAX 中轉 | 開最佳化後區域變數留在暫存器、跳躍表移到函式之前，分派收斂成單一條 `jmp cs:[reg*4+表]`——`FDPS.LE` 裡程式庫段的六張表正是這個形狀，遊戲段那張不是。`-od` 的前兩項相同，但引數一律直接推，見上 |
| `-oe=25`（自動展開小型 function，門檻 25） | 遊戲段有被展開的 function 副本，而原始碼層面不可能是關鍵字要求的（見下文「inline 展開」） | 不加 `-oe` 完全不展開；預設門檻（相當於 20）展開不到 `fdps_saf_frame_count`、`fdps_saf_get_frame`、`fdps_draw_unit_gauge_proportional`；27 以上會多展開原版保持呼叫的 `fdps_draw_gauge_fill`，33 以上連 `fdps_draw_gauge_bar` 也展開。24、25、26 三者產出相同 |

`-zq` 只影響訊息輸出，可加可不加。

### 旗標組不是全域統一的：CD 模組用的是另一組

上表那一組是**大部分**遊戲程式碼的旗標，不是全部。`0x3bade`–`0x3c93f` 這一段連續的 32 支 `fdps_cd*` 是用另一組編出來的：

```
CD 模組    wcc386 -bt=dos4g -mf -4s -fpi -os
其餘遊戲段  wcc386 -bt=dos4g -mf -4s -fpi -s -ot -oe=25 -d2
```

判定是逐 byte 的，不是推論：`src/cd.c` 的 12 支 function 以 `-os` 編譯有 4 支與原版逐 byte 相同（含 `0003bade` 那支 0x9f byte 的 `fdps_cd_alloc_dos_buffers`），`__CHK` 也在；以遊戲段旗標組（`-od` 或 `-d2`）編譯 0 支相同，形狀處處不同——四推框架對上沒有框架、`MOV EAX,offset` / `PUSH EAX` 對上 `PUSH imm32`、`XOR EAX,EAX` / `MOV AX` 對上 `MOVZX`，而且沒有 stack probe。`-os -d2` 同樣 0 支相同（`-d2` 把最佳化關掉，每支都變長），所以 CD 模組沒有帶 `-d2`。

範圍由 `__CHK` 的呼叫端界定：`get_xrefs_to 0004361a` 回來 34 個，32 個是位址連續的 `fdps_cd*`，另外兩個是 CRT 的 `spawnve` / `spawnvpe`；同一組 stub 的其餘三支，`__STK`（`0x4362d`）只被 `__CHK` 自己呼叫，`__GRO`（`0x4362a`）與 `0x43612` 沒有任何參照。「用 `PUSH imm32` 推資料位址」這個形式在整個 image 裡也只出現在同一個區塊。這個形狀只有「那是一個獨立的 translation unit，用不同旗標編」解釋得了——一支 function 的序幕不可能一半有 probe 一半沒有。

**重建版的建置目前還沒跟上這兩組旗標。** `build_emit.py` 對每個 unit 用同一組旗標，而且那一組仍是舊的 `-s -ot -od`，所以重建版的遊戲段沒有 `-oe`／`-d2`、CD 模組沒有 stack probe。行為上兩者等價（ADR-0001；probe 只在堆疊已經用爆時才讓行為分岔），對齊原版由票 22.2 處理：`-oe` 只在同一個 translation unit 內展開，照搬旗標之前 `src/` 的分檔必須先與原版的 unit 邊界一致，否則會展開原版沒展開的呼叫。在那之前，要逐 byte 對照原版就得自己另外用上面的旗標重編一份（`tools/build_flags/fn_match.py`），不能拿建置產出的 `.OBJ` 直接比。

### inline 展開來自 `-oe`，不是原始碼的關鍵字

**Watcom 10.0a 的 C 編譯器沒有 inline 關鍵字。** `_inline`、`__inline`、`inline` 三種寫法都以 `E1009: Expecting ';' but found 'void'` 失敗、不產出目的檔。所以遊戲段裡看到的 function 副本不可能是原始碼要求的，是 `-oe=25` 自動展開的。讀 assembly 時看到一支 function 的本體出現在呼叫端裡面，不能推論成「原始碼把它寫了兩遍」，也不能推論成「原始碼宣告了 `_inline`」。

指紋是**呼叫端的框架裡多出一份 callee 的完整框架**：呼叫端先把引數暫存值複製進一組連續的、參數形狀的槽，接著把 callee 的本體逐指令重放一次（槽位做統一替換），最後把結果從展開出來的結果槽複製到自己的區域變數。在 `-d2` 之下還有第二個指紋：展開副本裡的呼叫引數直接 `PUSH dword ptr [EBP-n]`，而同一句 C 在 out-of-line 副本裡經 EAX 中轉——因為展開配出來的槽是編譯器的暫存槽，不是宣告的變數。

遊戲段被展開的 function 有 11 支（依直接推送數排序）：`fdps_draw_unit_gauge_proportional`、`fdps_vfs_load_file_or_exit`、`fdps_object_set_field34_low_nibble_range`、`fdps_draw_gauge_bar_proportional`、`fdps_draw_stat_gauge`、`fdps_unit_get_item_id`、`fdps_blit_command_sprite`、`fdps_unit_is_retired`、`fdps_spell_heal_unit`、`fdps_spell_deduct_mp_cost`、`fdps_unit_mark_retired`；另有不帶呼叫、因而不留推送痕跡的 `fdps_pack_rgb`（展開在 `0002af60`）、`fdps_saf_frame_count` 與 `fdps_saf_get_frame`（展開在 `00014550`、`00014140`）。每一支都仍有 out-of-line 副本，被展開不代表它被刪掉；`0002af20` 的 `fdps_pack_rgb` 沒有任何呼叫端，正是「它的每一處使用都被展開掉了」的結果。

**`-oe` 的規則，逐條實測、與映像一致：**

| 規則 | 映像裡的實例 |
| --- | --- |
| 只展開同一個 translation unit 內的 callee | 被展開的 function 在別的 unit 的呼叫端一律是真的 `CALL`（`fdps_unit_get_item_id` 14 處、`fdps_blit_command_sprite` 42 處） |
| callee 定義在呼叫端之前或之後都展開 | `00010010` 展開了位址在它之後的 `fdps_unit_is_retired`（`000109b0`） |
| `&&`／`||` 的右運算元不展開 | `000107be`、`0002db22`：同一個 unit 裡其他用法都展開，只有這兩處是 `CALL` |
| 一個運算式裡有兩個呼叫時，左邊展開、右邊保持呼叫 | — |
| 門檻 25：大於門檻的 callee 不展開 | `fdps_draw_gauge_fill`、`fdps_draw_gauge_bar` 在同一個 unit 裡保持呼叫 |

`0002b034`／`0002b04a`／`0002b060` 三處呼叫 `fdps_get_rgb_red`／`_green`／`_blue`（36 byte，同一個 unit）保持真的 `CALL`，上表的規則還解釋不了，原因未定。

**分辨得出「inline 展開」與「巨集」，分辨不出「巨集」與「手寫兩遍」。** 展開會配置參數槽與結果槽，巨集是文字展開、不會配置；後兩者經過前處理之後是同一串 token，讀 binary 永遠分不開（`src/movegrid.c` 的 `00010c30` 就卡在這裡）。

**「寬度不符」只在形參型別比 dword 窄的時候才看得到，不能當成通用測試。** `fdps_pack_rgb` 的三個形參是 `unsigned char`，所以存進槽的是 dword（`MOV dword ptr [EBP-0x68],EAX`）而讀出來是 byte（`MOV AL,byte ptr [EBP-0x68]`）。`000144e0` 的形參是指標，沒有任何寬度不符可看，展開仍然成立。

**重建時兩種寫法都對。** ADR-0001 只要求功能等價：展開處寫成開碼算式，或寫成對同一個 unit 內 callee 的呼叫再交給 `-oe=25` 展開，行為相同。`src/` 目前是前者，因為建置還沒用 `-oe`；改成後者屬於票 22.2。

### 無法從 binary 判定的旗標

- **`-fp2` / `-fp3` / `-fp5` / `-fpr`**：在這組旗標下四者與不指定產生完全相同的機械碼，本 binary 沒有可分辨的痕跡。
- **`-zp`（結構對齊）**：編譯器預設等同 `-zp1`（實測 `struct {char a; int b; char c; short d; double e;}` 在預設下的欄位偏移是 0/1/5/6/8）。要確認原版是否另外指定，得等 struct layout 定案，屬於票 17。

### 引數推送形式：原版兩種都用，規則是「宣告的變數」對「展開的暫存槽」

把一個記憶體運算元推成呼叫引數有兩種寫法，原版**兩種都出現**：

| 形式 | 位元組 | 例 |
| --- | ---: | --- |
| 經 EAX 中轉 | 4（disp8） | `000192ee` `MOV EAX,[EBP-0x4]` / `PUSH EAX`，同一串連推四個 |
| 直接推記憶體 | 3（disp8） | `00019442` `PUSH dword ptr [EBP-0x20]`，同一串連推四個 |

遊戲段（`0x10000`–`0x3c000`）逐指令掃過一遍，2,587 個推送點的分佈是：

| | 區域變數（`[EBP-n]`） | 傳入參數（`[EBP+n]`） |
| --- | ---: | ---: |
| 直接推記憶體 | 150 | **0** |
| 經 EAX 中轉 | 1,353 | 1,084 |

**150 個直接推送全部落在 `-oe` 展開出來的副本裡**：107 個是展開配出的參數槽、43 個是展開本體的區域變數，來自上一節那 11 支被展開的 function。反過來，1,353 個經中轉的區域變數沒有一個在展開副本裡。這正是 `-d2` 的規則——程式設計師宣告的變數經 EAX 中轉，編譯器為展開配的暫存槽直接推——所以傳入參數從來不會被直接推。同一句 C 在 out-of-line 副本裡中轉、在展開副本裡直接推，每一處展開裡的呼叫，直接推送數都等於 out-of-line 副本在那個呼叫的記憶體引數數。

`-od` 產出的全是直接形式（2,587 處只對上 150），`-oe -d2` 對上全部 2,587 處。

**重建時不必處理。** ADR-0001 明講指令選擇不在等價標準內，兩種形式行為相同。在重建版還用 `-od` 的期間，**原版用中轉形式的那些呼叫上，重建 body 會比原版短，差額是該處記憶體引數個數乘以 1 byte**，所以拿 body 長度差當「轉錄漏了東西」的訊號時要先扣掉這一項。實例：`000192c0` 原版 `0x4d`、重建 `0x49`，四個引數。

### 逐指令對得上的一段

`0x2f6d0` 那個 8-case switch 是整組旗標的收斂點，用上表的旗標重編一份等價的 C，出來的指令序列與位元組填充完全一致：

```
FDPS.LE 0x2f6d0                          wcc386 -mf -4s -fpi -s -ot -oe=25 -d2
  push ebx / esi / edi / ebp               push ebx / esi / edi / ebp
  mov  ebp,esp                             mov  ebp,esp
  sub  esp,0x10                            sub  esp,<n>
  jmp  short（跳過表）                     jmp  short L2
  8b c0（兩 byte 填充，對齊表）            mov  eax,eax
  <8 筆跳躍表>                             L1 DD ...×8
  cmp  dword ptr [ebp+0x34],7              cmp  dword ptr +14H[ebp],7
  ja   <default>                           ja   near ptr L11
  mov  eax,dword ptr [ebp+0x34]            mov  eax,dword ptr +14H[ebp]
  lea  eax,[eax*4 + 0]                     lea  eax,+0H[eax*4]
  jmp  dword ptr cs:[eax + 0x2f6e0]        jmp  dword ptr cs:L1[eax]
```

## 連結指令

```
system dos4g
name FDE.EXE
option stack=8k
file fde.obj
file <其餘 .obj>
library clib3s.lib
library math387s.lib
library emu387.lib
```

| 項目 | 依據 |
| --- | --- |
| `system dos4g` | LE header 的 CPU type 2／OS type 1、三個 `BIG32` object、DOS/4G stub。這個 system 定義**不會自動帶任何 C runtime**，三個 lib 必須自己列 |
| `option stack=8k` | DGROUP 的 `STACK` 段是 8,192 byte（範圍見 [`memory_layout.md`](../program_info/memory_layout.md)），初始 ESP 指向段尾。wlink 的**預設是 4K**，實測不寫這行只會拿到 `0x1000` |
| 連結輸出叫 `FDE.EXE`，含 `main` 的模組是 `fde.obj` | LE 的 resident name table 是 `fde`。實測 wlink 把這欄填成輸出檔的主檔名，而沒有 `name` 指令時輸出檔名又取自第一個 `.obj`。所以這一個觀察無法分辨「明寫了 `name FDE.EXE`」與「沒寫 `name`、第一個 obj 叫 `fde.obj`」——但兩條路都會產生相同的 header，重建時擇一即可。無論哪一條，`FDPS.EXE` 都是事後改名 |
| 三個 lib | 見上節的 byte 比對。`clib3s`／`math387s` 的 `s` 後綴就是堆疊呼叫慣例的版本，這是 `-4s` 在連結層的獨立佐證 |
| 沒有 debug directive | 執行檔尾端沒有 wlink 的 Watcom 除錯資訊（帶 `DEBUG` 時會附上一段以 `0x8386` 結尾的尾碼）。LE header 的 `debug_info_off` 不能當證據：10.0a 的 wlink 即使帶 `DEBUG ALL` 也把它留 0 |
| stub 用預設的 `wstub.exe` | stub byte 與 10.0a 產出的完全相同 |

object 3（`0x70000`，84 byte）不是上面任何一段產生的——它是某個 vendor 模組自帶的、不屬於 DGROUP 也不屬於 CGROUP 的資料段（取用範圍見 [`memory_layout.md`](../program_info/memory_layout.md)）。它掛在哪個 lib 上屬於票 14／19。

## 手寫組語：原版不是 WASM 組的，但 WASM 寫得出同樣的指令序列

RLE 繪製的 15 支（`fdps_blit_dispatch` 與它底下的 14 支，範圍見 [`program_info/code_pools.md`](../program_info/code_pools.md)）在重建裡保留原版組語，以 `wasm` 組譯。標準是**指令序列逐道相同、每道指令長度相同**，byte 只允許在「同一道指令的另一種等長編碼」與重定位欄位不同；檢查工具是 [`tools/rle_asm/asm_match.py`](../tools/rle_asm/_index.md)，已登記進 build gate。

### 原版的組譯器不是 WASM 10.0a

兩個特徵都在這 15 支裡逐道量過：

| 特徵 | 原版 | WASM 10.0a |
| --- | --- | --- |
| 暫存器對暫存器的 `mov`／`add`／`sub`／`xor`／`and`／`or`／`cmp` | 目的運算元放在 ModRM.reg 的那一個 opcode，例：`or bx,bx` 是 `66 0B DB` | 另一個方向，`66 09 DB` |
| `cmp ax,0` | 累加器形式 `66 3D 00 00` | imm8 形式 `66 83 F8 00` |
| 前向分支 | 先預留長形式，距離夠短就縮成短跳，**省下的 byte 用 NOP 補回**：條件跳躍 2 byte + 4 個 `NOP`，`JMP` 2 byte + 3 個 `NOP`；目標太遠才留長形式 | 多趟組譯，直接排出最短形式，不留填充 |
| 後向分支 | 距離已知，直接取最短形式，沒有填充 | 同左 |

前兩列長度相同、CPU 的動作與週期相同，屬於允許的差異，**照一般寫法寫即可，不能為了對齊 byte 改寫 `DB`**。第三列是單趟組譯器的指紋：15 支共 505 個 `NOP`，全部是前向短跳後的填充，沒有任何分支的目標落在 `NOP` 上。

### 用 WASM 寫出同樣序列的規則

- **前向短跳**寫 `jcc short 標籤`／`jmp short 標籤`，後面照原版逐行寫 `nop`。條件跳躍沒跳時會順著執行這些 `NOP`，省掉就改變了執行的指令數。
- **其餘分支不指定長短**，讓 WASM 自己選：後向分支與超出短跳範圍的前向分支都會出原版的長度。
- **例外：原版是前向長跳、但縮成短跳後目標就進得了短跳範圍的條件跳躍。** 原版的規則以「保留長形式時的距離」判斷，WASM 以「自己縮短後的距離」判斷，所以 32-bit 位移 D 落在 124–127 的前向 `jcc`，WASM 不指定長短時一律組成短跳。這時要寫 `jcc near ptr 標籤` 強制長形式。本遊戲只有兩處（`0x57801`、`0x57984` 的 `jb`，D 都是 127）。同樣處境的 `JMP` 實測 WASM 自己保留長形式（D=126 的 `0x56e89`、`0x5718a`），不必處理。
- **`jcc near ptr` 的位移有錯**，只在 D 夠大時才對。實測（`jb near ptr`，D 以 6 byte 長形式的結尾起算）：

  | 方向 | 組錯（目標多 1 byte） | 組對 |
  | --- | --- | --- |
  | 前向 | D ≤ 126 | D ≥ 127 |
  | 後向 | 跳過的 byte 數 ≤ 123 | ≥ 124 |

  所以 D=124–126 的前向長 `jcc` 在 WASM 裡沒有乾淨的寫法；本遊戲沒有這種分支。`jmp near ptr` 的位移一律正確。
- **標籤是整個模組共用的**，同一個 `.asm` 裡的每支 routine 要用各自的前綴（前綴表在 `asm_match.py` 的 `ROSTER`），否則 `E071: Symbol already defined`。
- **資料以 `extrn 符號:word` 之類宣告後直接用符號**，不需要 `.model flat`。WASM 對外部符號產生的 fixup 以目標決定 frame，wcc386 用 `FLAT` group；實測兩種在 DOS/4G 的 LE 裡解出同一個位址。
- 一個檔裡兩支 routine 之間的跳躍在接成一個模組後不再是 fixup，距離可能變短，所以落地後要以接好的檔重新比對一次（`asm_match.py check --fresh`）。

### 與 C 呼叫端的契約

分派者照原版保存 EBX／ESI／EDI／EBP、破壞 EAX／ECX／EDX，正是 `-4s` 堆疊慣例的 callee-saved 集合，所以 `blit.h` 的 `#pragma aux fdps_blit_dispatch "*" parm caller [];` 不必加 `modify`。重建以 `-od` 編譯，呼叫端本來就不把值留在暫存器裡跨過呼叫。

## 個別 function 的 calling convention

**預設是堆疊慣例（`-4s`），但不能假設全域統一。** 每個 function 的 cc 必須在 emit 時於程式碼中明確宣告，不靠旗標帶過。

遊戲本體那 514 個已經逐一判定完畢：`__cdecl` 503 個、`__watcall` 11 個，而 11 個全部是遊戲自己寫的組合語言。也就是說 `wcc386` 產出的遊戲程式碼**沒有一個例外**，全部走堆疊慣例。Ghidra 對整個 binary 標的 `__watcall` 是自動分析的預設值，與這裡的量測相反，不能拿來當依據。

序幕形狀的量測（樣本為 1,042 個 function）：

- 468 個是標準的四推序幕（其中 414 個在 `0x3b000` 以下的遊戲段）——堆疊慣例
- 18 個序幕就是 `push imm` / `call`，同樣是堆疊慣例。`push imm` / `call __CHK` 這種序幕的主體是帶 probe 的 CD 模組，不是程式庫：`__CHK` 的 34 個呼叫端是 32 支 `fdps_cd*` 與 CRT 的 `spawnvpe`（`0x54dcc`）、`spawnve`（`0x556ae`）。`0x51f6b` 的序幕呼叫的是別的東西
- 其餘 556 個是手寫組語或開了最佳化的 vendor 程式碼，形狀各異
- 全 binary 只有 2 個 function 含 `RET imm`（`0x4361a` 的 `__CHK`、`0x5038a`）

辨識偏離預設的 function，訊號強度由強到弱：

1. **呼叫端在 `CALL` 之後沒有 `ADD ESP,n`，但有引數被傳進去**——引數不在堆疊上
2. **callee 以 `RET n` 結束**——callee 自己清引數，不是預設慣例
3. **呼叫端在 `CALL` 之前設定 EAX／EDX／EBX／ECX**，且 callee 在序幕之後立刻讀這幾個暫存器
4. **callee 在 entry 就把 EBX 當輸入讀而沒有先 `PUSH`**——堆疊慣例下 EBX 是 callee-saved，這是強烈的 register 慣例訊號
5. 序幕不是四推、或引數不在 `[ebp+0x14]` 起——至少不是遊戲模組的預設形狀

判斷時要**跳過 `push imm` / `call __CHK` 這兩條**，它們是堆疊檢查不是 cc 訊號；`__CHK` 刻意保留 EAX／EDX／ECX／EBX，讓 register 引數能安然通過。含 varargs 的 function 一律是堆疊慣例。

## 重現方式

[`tools/build_flags/`](../tools/build_flags/_index.md) 收了旗標組的全部判定腳本。所有 Watcom 工具都在 DOSBox-X 裡以 DOS 版執行，與原版的建置環境一致。`verify_flags.py` 會用上表的旗標組編譯探針並逐項比對本檔列出的 15 個特徵，旗標組若被改動就重跑它。

`-oe=25 -d2` 的判定由另外三支重現：`oe_probes.py` 跑關鍵字、展開與推送形式、函式表間接呼叫、呼叫位置規則的探針，並直接從映像數函式表呼叫的形狀；`fn_match.py` 把真實的 `src/` 檔用指定的旗標組編譯，與 `FDPS.LE` 裡同一支 function 的原始 byte 逐 byte 對照（遮掉 LE 重定位欄位與 `E8` 呼叫目標、去掉尾端的對齊填充），上文「42 支中 6 支／17 支相同」就是它對 `palette.c`、`saf.c`、`gauge.c`、`chevt6.c`、`sprite.c`、`maptile.c`、`palcycle.c` 的結果，CD 那幾個數字是它對 `cd.c` 的結果；`oe_threshold.py` 掃 `-oe=N`，得出 24–26 的區間。

工具鏈版本那一節的量測在 [`tools/crt_version/`](../tools/crt_version/_index.md)：程式庫拆解、逐 function 的版本掃描、逐一判定的 workflow、交集，以及編譯器的差分編譯都在那裡。
