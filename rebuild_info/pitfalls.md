# 重建踩雷點

「照直覺寫就會與原版不同」的事項總表。每一條都是原版做了一件不合常理、而重建時很容易順手改掉的事。

本檔擁有的是「這件事在重建時會出錯」這個判斷與對策；事實本身由連結指向的正典文件擁有，這裡不重複佈局與數值。收錄門檻是**照著現代直覺或編譯器慣例寫就會偏離原版**，單純「這裡很複雜」不收。

分成五類：

- **不能修的原版 bug**：原版寫錯了，但外顯行為依賴它，或至少不能無聲地改掉。功能等價的定義見 [ADR-0001](../docs/adr/0001-only-functional-equivalence.md)。
- **不能加的檢查**：原版沒有做的驗證，補上去會讓原本能跑的輸入被擋掉。
- **不能換的型別與寫法**：語意上「等價」但實際行為不同的替換。
- **不能照字面理解的資料**：資料表的欄位語意與欄位名稱或直覺對不上，照字面用會算錯。
- **不能照編譯器慣例設定的旗標**：用預設值或沿用前作的旗標會產生行為不同的執行檔。

## 單一 function 的踩雷點記在它自己的註解裡

本檔收的是**跨 function 反覆出現的模式**。只影響一支 function 的細節寫在該 function 的 plate comment 尾端、標題為 `Rebuild note:` 的那一段，隨 [`ghidra_snapshot/comments.txt`](../ghidra_snapshot/comments.txt) 進版控。

分界在於重複次數：「未初始化的 tick latch」在十幾支動畫迴圈裡是同一個陷阱，屬於本檔；「章節 15 的結束處理清的是 byte +0x34 而不是 byte +5」只有一處，屬於那支 function 的註解。寫 C 的人手上會同時有這份總表與該 function 的註解，兩邊都看得到。

## 不能修的原版 bug

| 事項 | 照直覺會怎麼寫 | 正典 |
| --- | --- | --- |
| `.SAF` 的 magic 檢查是 `p[0]=='S' \|\| p[1]=='A' \|\| p[2]=='F'`，三個條件是 OR | 寫成 `&&` 或 `memcmp`。改了之後原本放行的檔會被擋下 | [`resource_info/saf.md`](../resource_info/saf.md) |
| `.CEL`／`.SAF` 的繪製器逐列扣 column 數，一個 op 超出列尾會讓計數繞回成極大值並寫穿記憶體 | 加上 clamp 或提早 break。原版是靠編碼端保證每列剛好填滿，繪製器本身不設防 | [`resource_info/cel.md`](../resource_info/cel.md) |
| 職業表的索引一律是「職業代碼 + 1」，但 `0x126b0` 這一處漏了 `INC`，拿到的是前一個職業的地形消耗 | 統一成 `promap[class + 1]`。這處走的是「這個單位走不走得到目標格」的判斷，與實際移動用的表不同，改了行為就不一樣 | [`assets/tables/classes.md`](../assets/tables/classes.md) |
| 動畫迴圈的「上一次的 tick」區域變數**在寫入之前就被讀取**，所以第一格不等待。十幾支迴圈都是這個形狀（轉場、戰鬥數值條、SAF 播放、村莊行走） | 在迴圈前寫 `int last = tick;`。這是最自然的修法，而它讓每一段動畫都多一個 tick | plate comment 的 `Rebuild note` |
| 越界寫入是原版行為：狀態視窗的邊框清除迴圈跑 320 圈而畫面只有 200 列（超出 mode 13h 尾端 38KB）、商店的下箭頭 blit 超出 malloc 區 10 列、單位陣列搬移的 `memmove` 比來源多讀一筆 0x50 記錄 | 把長度統一成正確值。這些寫入落在堆積或顯示卡孔徑上，改了之後被踩掉的內容跟著變 | plate comment 的 `Rebuild note` |
| 記憶體管理的原版錯誤要照留：片尾名單每張卡片配置 89KB 卻只在迴圈外 free 一次、商店的移動網格在迴圈底部才配置而在頂部讀取（第一圈讀未初始化的堆疊、之後讀已 free 的區塊） | 把 `free()` 移進迴圈、把配置提到迴圈外。後者只是「碰巧能跑」——Watcom 的近端堆積會把同尺寸的區塊原樣還回來 | plate comment 的 `Rebuild note` |
| 鍵盤環形緩衝區沒有滿檢查，寫索引追上讀索引之後 `fdps_read_keyboard_queue` 回報空佇列，而裡面積著十個未讀掃描碼 | 加一個計數或滿檢查。改了之後遊戲收到的按鍵序列就不一樣 | plate comment 的 `Rebuild note` |

## 不能加的檢查

| 事項 | 照直覺會怎麼寫 | 正典 |
| --- | --- | --- |
| `.VFS` 開啟時完全不驗證 magic、版本與簽章 | 開檔先 `memcmp` magic。原版餵一個非 VFS 檔進去不會被擋下 | [`resource_info/vfs.md`](../resource_info/vfs.md) |
| `.CEL` 的偏移表位置寫死 `+0x0F`，header 的 `0x05` 欄位從不讀 | 改成讀 `0x05` 當表位置。兩者目前恰好都是 15，改了在畸形檔上行為就不同 | [`resource_info/cel.md`](../resource_info/cel.md) |
| `.CEL` 的像素格式欄位 `0x0D` 從不讀，全程只有一條解碼路徑 | 依 `0x0D` 分派兩種解碼器。原版會把 `M310.CEL` 當 4-op RLE 讀，這個矛盾未收斂，見 [`open_issues.md`](../open_issues.md) | [`resource_info/cel.md`](../resource_info/cel.md) |
| 九張 `.DAT` 資料表的取值一律是 `base + index * stride`，沒有任何上界檢查 | 加上 `index < count`。物品編號 `FF` 就落在 `ITEM.DAT` 之外，遊戲裡確實拿得到這個編號，效果隨當時的堆積內容而變 | [`assets/items.md`](../assets/items.md) |
| 建立我方單位時，`FRIAPRDA.DAT` 前兩個物品槽無條件標成「裝備中」，不看值是不是 `FF` | 依值判斷空槽再決定狀態 | [`assets/tables/characters.md`](../assets/tables/characters.md) |
| 數值條的 clamp 是**單邊**的：下界壓到 0，上界不壓，而呼叫端算出的 `(cur * width + max - 1) / max` 在 `cur > max` 時會超出 | 補上對稱的 `min(width, fill)`。原版的滿溢數值條有的整條不畫、有的畫成超長的糊塊，補了 clamp 就變成乾淨的滿格 | plate comment 的 `Rebuild note` |
| 視野裁切在兩軸上不對稱（x 兩邊都是嚴格不等式、y 兩邊都含端點），格子繪製的四邊界則全部嚴格，不合格的格子整塊丟掉而不是裁切 | 寫成對稱的、或寫成「超出就裁切」。貼齊畫面邊緣的那一row/column 會出現在原版沒有畫的地方 | plate comment 的 `Rebuild note` |
| 音效索引 `-1` 是活的輸入值：配置器在音效關閉或八個聲道全忙時回 `-1`，呼叫端不檢查就往下送，原版於是讀到 handle 表前面那個 dword | 加上 `if (index < 0) return;`。這個保護只有在確認過每個呼叫端之後才安全 | plate comment 的 `Rebuild note` |
| WAV header 的解析結果被忽略，非 RIFF 的緩衝區會以未初始化的 14-byte 堆疊描述子播放出去；chunk 走訪也沒有 RIFF 的偶數對齊與邊界檢查 | 補上「解析失敗就回 -1」與正確的 RIFF 走訪 | plate comment 的 `Rebuild note` |

## 不能換的型別與寫法

| 事項 | 照直覺會怎麼寫 | 正典 |
| --- | --- | --- |
| `.VFS` 成員查找是**單向**轉大寫：把傳入的名稱就地轉大寫，entry 名稱原樣取用，兩者 `strcmp` | 寫成 `stricmp(entry, query)`。遇到非全大寫的 entry 名稱行為就不同，而且原版會就地改寫呼叫端的緩衝區，這個副作用是可見的 | [`resource_info/vfs.md`](../resource_info/vfs.md) |
| `.VFS` 的 entry 筆數以 8-bit 讀入，第 256 筆以後走不到；entry table 偏移以帶號 16-bit seek，上限 `0x7FFF` | 用 `u32` 讀筆數、用 `long` seek。容器沒有踩到上限，但這是原版的硬限制 | [`resource_info/vfs.md`](../resource_info/vfs.md) |
| `.SAF` 的 tilemap 格子編號是 `i16`（`short *` 取值、`-1 < index` 擋下界），但 layer 的 tilemap 編號是零延伸的 `u16` | 兩個都寫成同一種索引型別 | [`resource_info/saf.md`](../resource_info/saf.md) |
| `.SAF` 的 layer 半透明程度以 16-bit `MOVSX` 讀 `+0x07`，連 `+0x08` 的保留 byte 一起讀進來 | 宣告成 `u8`。保留 byte 恆為 0，所以目前無差別，但欄位的實際寬度是 2 | [`resource_info/saf.md`](../resource_info/saf.md) |
| 章節音軌表的位元組要 **+1** 才是 MSCDEX 音軌編號，加法由呼叫端在起播前做，不在表裡 | 直接把表值當音軌編號送出去，整首曲子會差一軌 | [`program_info/cd_audio.md`](../program_info/cd_audio.md) |
| CD 命令的 `INT 2Fh` 不是指令，是 DPMI `INT 31h` AX=0300h 的 real-mode call structure 裡的資料位元組 | 直接寫 `int 0x2f` 內嵌組語。在 DOS/4G 保護模式下走不通 | [`program_info/cd_audio.md`](../program_info/cd_audio.md) |
| `0x63930` 以後的 global 全部在 BSS，其中 `0x64000` 之後執行檔裡連內容都沒有 | 照 Ghidra 顯示的零值 emit 成初始化陣列。BSS 從 `0x63930` 起就該宣告成未初始化，而 `0x64000`–`0x6c3bf` 這 33KB 更是連檔案裡都不存在，載入器補的零與檔案帶的零長得一樣，照抄會把它們塞進映像檔 | [`program_info/memory_layout.md`](../program_info/memory_layout.md) |
| AIL 的每一條公開宣告都要掛 `#pragma aux AIL_<fn> "*" modify [eax ebx ecx edx];` | 照 C 的常識寫成 `extern void AIL_startup(void);` 就算。AIL 的 vendor object 不是 `wcc386` 的輸出，它會在沒有存回的情況下蓋掉 EBX／ECX／EDX；少了 modify 清單，編譯器會把活值留在 EBX 跨過 AIL 呼叫，值被無聲吃掉，沒有任何診斷，而且與該進入點宣告成 `__cdecl` 還是 `__watcall` 無關 | [`program_info/code_pools.md`](../program_info/code_pools.md) |
| `__sys_init_387_emulator`（`000444a4`）以 **EBP** 收一個活的旗標，跳進它的 `0003d50a` 把 EBP 原樣轉發過去 | 宣告成 `void __sys_init_387_emulator(void)`。任何 C 原型都表達不了這個介面，寫成 C 之後編譯器會自己配置 EBP，旗標就傳不進去 | [`program_info/code_pools.md`](../program_info/code_pools.md) |
| 遊戲的 blit kernel 家族是手寫組語：沒有 prologue，參數由呼叫端預先放在 ESI／EDI／ECX／EDX，共用呼叫端的 EBP frame，還會蓋掉呼叫端的傳入參數槽 | 照 Ghidra 推出來的 `__watcall` 簽章寫成一般 C function。那個簽章是反編譯器猜的，不是真的呼叫慣例；寫成 C 之後編譯器會自己配置 frame 與暫存器，這個以暫存器交接的契約就斷了。必須以 `.ASM` 模組或內嵌組語產出 | [`program_info/code_pools.md`](../program_info/code_pools.md) |
| DGROUP 最上面的 8KB（`0x6a3c0` 以後）是堆疊段，不是 global | 看到 Ghidra 在那裡標了位址就當成 BSS 變數 emit。真正的 BSS 在 `0x6a3bc` 就結束了，那一段是堆疊、環境變數複本與近端堆積共用的空間 | [`program_info/memory_layout.md`](../program_info/memory_layout.md) |
| 存檔的 checksum 只加總 **`len - 4`** 個 byte，尾端 4 byte 的 checksum 欄位本身不算進去（`0x56898`） | 加總整個緩衝區。舊存檔一律驗不過 | [`program_info/code_pools.md`](../program_info/code_pools.md) |
| `FDE.SAV` 的 XOR 串流密鑰是硬寫的：DX 起始 `0xa5`，每個 byte 先 `DX += 0x9014` 再 `ROL DX,3`，取 DL 與資料 XOR（`0x568b7`） | 換一組看起來等價的常數或改變運算順序。加解密是同一支常式，改了之後新舊存檔互不相容 | [`program_info/code_pools.md`](../program_info/code_pools.md) |
| Watcom 的 `printf` 認得 `%hf`／`%hF`，那是 **16.16 定點數**轉換（吃 32-bit 整數、預設精度 4、完全不碰 FPU），不是 `%f` 的短版 | 轉錄格式字串時把 `%hf` 當成筆誤改成 `%f`。輸出數值會變，而且會把浮點格式化支援拉進映像檔 | [`program_info/code_pools.md`](../program_info/code_pools.md) |
| timer ISR 遞增的 tick 計數器 `0x69d64` 在每一支動畫的空轉迴圈裡都要宣告成 `volatile` | 當成普通 global 讀。最佳化器會把載入提到迴圈外，遊戲在第一個等待點就永遠停住 | plate comment 的 `Rebuild note` |
| 單位記錄的狀態 byte `+5` 有兩種寫法且不能互換：退場是**整個指派** `rec[5] = 1`，而「本回合已行動」是 `rec[5] \|= 0x80` | 統一寫成 `\|=` 與 `&= ~`。退場改成 OR 之後，已行動旗標會留在一個已經退場的單位上 | plate comment 的 `Rebuild note` |
| 單位記錄的 `+0x34` 是 packed byte：低 nibble 是行為模式，高 nibble 是別處會測的旗標，所以設模式一律是 `rec[0x34] = (rec[0x34] & 0xf0) \| mode` | 把它當成單純的模式欄位寫 `rec[0x34] = mode`，高 nibble 的旗標被無聲清掉 | plate comment 的 `Rebuild note` |
| 繪製順序與快照時機是行為的一部分：乾淨背景一律在內容畫上去**之前**取樣，地圖單位的影子全部畫完才畫第一個 sprite | 把「畫完再取樣」寫成比較自然的順序、或把兩趟掃描合併成一趟。前者讓每次重繪都疊上舊高亮，後者讓後面的單位把影子蓋到前面的單位身上 | plate comment 的 `Rebuild note` |
| VGA 的垂直歸線等待是**兩段式**：先等 `0x3da` bit 3 變 1，再等它變回 0 | 只等 bit 3 變 1 就開始複製。每次複製會提早一個消隱期開始，轉場的樣子跟著變 | plate comment 的 `Rebuild note` |
| 資料表的記錄要 packed：`MAGICDAT.DAT` 的一筆是 7 byte 而開頭是 16-bit 欄位 | 宣告成自然對齊的 struct 再用 `table[id]` 取值。stride 會變成 8，從第 1 號法術起全部讀到錯的記錄 | [`assets/tables/spells.md`](../assets/tables/spells.md) |
| VFS 查找內部的 `strupr` 會**就地改寫呼叫端的緩衝區**，而呼叫端傳的是字串字面值——映像檔裡的 `"Turn.saf"` 在第一次呼叫後永久變成 `"TURN.SAF"` | 把參數宣告成 `const char *`（過不了編譯），或把字串字面值放進唯讀儲存區（執行時會當掉） | [`resource_info/vfs.md`](../resource_info/vfs.md) |
| DPMI 鎖頁的 `end` 是**範圍最後一個 byte**，送給 DPMI 的長度是 `(max - min) + 1`；`fdps_dpmi_lock_size(base, size)` 因此鎖的是 `size + 1` byte | 寫成 `size = end - start`，或把 wrapper 改成半開區間的 `base + size - 1`。少鎖一個 byte，而那個 byte 剛好落在頁邊界時 AIL 的中斷處理會踩到未鎖的頁 | plate comment 的 `Rebuild note` |
| 反查調色盤立方體的 12-bit 索引順序是 **green:red:blue**，不是 RGB | 寫成 `(r << 8) \| (g << 4) \| b`。查表本身還是查得到顏色，只是查到的是另一個 | plate comment 的 `Rebuild note` |
| CD 那一段有五支 function 的 body 裡**沒有 `RET`**：控制流以 `JMP` 落進鄰居的 body 借用它的收尾段（`0003bd99`、`0003be36`、`0003c4ff`、`0003c6bc`、`0003c7aa`）。那是 `wcc386` 把兩支近乎相同的 C function 的尾端合併掉的結果 | 照反組譯逐條轉錄、寫到最後一條指令就停。合併掉的那一段是這支 function 的 C 原始碼的一部分，漏掉它就漏掉尾端的儲存動作。反過來說也不能因此改邊界——把尾巴併回來會毀掉另一支 function 跳進去的目標 | [`program_info/code_pools.md`](../program_info/code_pools.md) |

## 不能照字面理解的資料

| 事項 | 照直覺會怎麼寫 | 正典 |
| --- | --- | --- |
| 出場數值的成長公式左右不對稱：AP／DP／DX 乘 `LV`，HP／MP 乘 `LV - 1` | 兩組都寫成同一個 `LV - 1`。這不是筆誤，12 名人物的攻略站數值全部只在這組公式下吻合 | [`assets/characters.md`](../assets/characters.md) |
| `FRIAPRDA.DAT` 的 60 筆不是 60 組角色屬性 | 用肖像編號索引整張表，看到有數值就當成該索引的出場屬性。只有 `00`–`0B` 是獨立內容，`0C` 以後是四組重複的樣板列，`32`–`3A` 又是 `00`–`08` 的逐 byte 複本——拿到的數字看起來完全合理 | [`assets/characters.md`](../assets/characters.md) |
| `FRIAPRDA.DAT` 的 `level` 欄位不是人物實際出場的等級 | 直接拿它當出場等級。實際等級寫在地圖單位記錄裡，法蓮娜的欄位是 3 而她以 8 級加入 | [`assets/characters.md`](../assets/characters.md) |
| 成長範圍的 `*_max` 是「最大成長值加 1」 | 拿它當實際拿得到的最大成長，每一級都會多算 1 點 | [`assets/tables/characters.md`](../assets/tables/characters.md) |
| `ITEM.DAT` 的 23 個 byte 不是物品行為的全部 | 假設把這張表搬過去物品就完整了。每回合回復、以及生命之實與三種藥水這類永久強化的**幅度**都不在 record 裡——它們的 `use_effect` 有值而 `use_amount` 是 0 | [`assets/items.md`](../assets/items.md) |
| 職業表的魔抗欄位存的是 100 減去魔法抗性 | 直接當抗性用，抗性高低會完全顛倒 | [`assets/tables/classes.md`](../assets/tables/classes.md) |
| 法術的威力欄位為負數時是攻擊力加乘率的百分比，不是傷害 | 宣告成 `u16` 或直接當傷害用。八個絕招全部靠這個負值表示加乘 | [`assets/tables/spells.md`](../assets/tables/spells.md) |
| `Icon%02d.dat` 的編號是**章節索引**（0 起算），而章節處理函式以玩家看到的章號命名，兩者差 1——`fdps_chapter_12_init` 載入的是 `Icon11.dat` | 照 function 名稱裡的章號寫檔名。三十支處理函式全部會播到下一章的開場動畫，而且照樣編譯照樣跑 | [`CONTEXT.md`](../CONTEXT.md) |
| `0x640d8` 起的 0x20 byte 是**章節共用**的事件旗標陣列：`fdps_chapter_state_reset` 每次進章節整塊 memset，讀檔時整塊還原，十幾支不同章節的處理函式各自latch 其中一個 byte | 把它寫成該處理函式裡的 `static char done`。那是 assembly 看起來的樣子，但重來一章時不會被清掉，讀檔也還原不了 | plate comment 的 `Rebuild note` |
| CD 的 MSF 換算已經扣掉 150 frame 的 pregap：`fdps_cd_msf_to_sector` 回的是邏輯磁區號，`fdps_cd_sector_to_msf` 又再扣一次 150（因為它的輸入已經是扣過的） | 寫教科書版的 `minute*60*75 + second*75 + frame`。每一軌的起點都會差 150 frame，長度查詢則會少兩秒 | [`program_info/cd_audio.md`](../program_info/cd_audio.md) |
| 三十支章節 init 處理函式看起來一模一樣，但**不能用迴圈或樣板生成**：`Icon%02d.dat` 的編號差 1、`fdps_roster_add_character` 必須排在 `fdps_chapter_state_reset` 之前（reset 會依名冊人數重建地圖單位，順序反過來新加入的角色會被歸零成退場）、而且第 17／22／23 章傳的游標目標不是 0 | 用一支樣板產生三十支。前兩項會讓某些章節少一個角色或播錯動畫，第三項只影響三章 | plate comment 的 `Rebuild note` |

## 不能照編譯器慣例設定的旗標

旗標組本身與判定依據見 [`build_flags.md`](build_flags.md)，這裡只收「不照原版設會出事」的五項。

| 事項 | 照直覺會怎麼寫 | 正典 |
| --- | --- | --- |
| 程式庫要用 10.0a 的，10.0 家族的三個發行版不能互換 | 手上裝了哪個 10.0 就連哪個，反正都是 10.0 家族。10.0b 的 `MATH387S.LIB` 把 `strtod` 重編成大 4 byte 的框架，又把 5 byte 的裸 `IF@TAN` 換成 21 byte、會回退到軟體實作的守衛版；10.0 的 `CLIB3S.LIB` 則有另一套 `__prtf`／`__scnf`／`__isindst`／`_nmalloc`。連錯版本不會有任何診斷，映像檔就是另一份 | [`build_flags.md`](build_flags.md) |
| 遊戲模組用 `-s` 關掉堆疊檢查 | 不加 `-s`，用編譯器預設。預設會在每個有框架的 function 前插入 `push <大小>` / `call __CHK`——原版的遊戲碼在堆疊耗盡時是直接寫穿，重建版會改成印 `Stack Overflow!` 然後結束，外顯行為不同。程式庫模組本來就帶檢查，那 17 個要照留 | [`build_flags.md`](build_flags.md) |
| `-ot` 要寫在 `-od` 前面 | 只寫 `-od`，或寫成 `-od -ot`。`wcc386` 由左而右處理選項：`-ot` 設定「以速度為優先」的偏好，`-od` 之後才關掉最佳化器而不清掉那個偏好。只寫 `-od` 會讓所有位址縮放從 `lea reg,[reg*N]` 變成 `shl reg,N`（原版有 304 處）；寫成 `-od -ot` 則會連最佳化器一起打開，區域變數不再來回堆疊 | [`build_flags.md`](build_flags.md) |
| 原版用 `-fpi` 而不是 `-fpi87` | 沿用前作 FD2 的 `-fpi87`。wlink 只抽出解得掉未定義符號的 lib 成員，`-fpi87` 不會發出 `__init_387_emulator` 這個參照，於是 `emu387.lib` 就算在 `.lnk` 裡列了也不會被連進去——在沒有 387 的環境下遊戲的浮點運算直接當掉 | [`build_flags.md`](build_flags.md) |
| 連結要明寫 `option stack=8k` | 不寫，讓 wlink 用預設。wlink 的預設是 4K，只有原版的一半 | [`build_flags.md`](build_flags.md) |
| 原版用 flat 記憶體模型（`-mf`），const 資料與區域陣列初值影像因此落在程式碼 object | 用 `-ms`。除了資料搬家到 DGROUP 之外，每次把初值複製到堆疊前還會多兩條 `mov ax,ss` / `mov es,ax`。反過來說，讀 Ghidra 時看到常數表夾在函式之間也不要當成分析錯誤 | [`build_flags.md`](build_flags.md) |

## 環境與範圍

| 事項 | 內容 | 正典 |
| --- | --- | --- |
| 本機 `WATCOM_10.0a` 的 `lib386\dos\clib3s.lib` 是殘缺的副本 | 它比同一發行版的完整副本少一個模組（`stk386`，393 對 394），少掉的正是 `__CHK`／`__STK`／`__GRO`／`__STKOVERFLOW` 這組堆疊檢查 stub。遊戲模組用 `-s` 不會參照它們，但照留堆疊檢查的那 17 個程式庫模組會，連結時就是解不掉的外部符號。建置與比對都要改用 `WATCOM_10.0a_infobase` 那份，或先把檔案補回去——兩份安裝共有的 393 個模組 SHA-256 全數相同，其餘四個程式庫也是模組對模組、雜湊對雜湊一致，所以換過去不改變任何結論 | [`build_flags.md`](build_flags.md) |
| 啟動的三道光碟檢查 | `access("DISK.NO")`、由 `Disk.no` 第三個 token 取得路徑前綴、MSCDEX 安裝檢查，任一不過就 `exit(1)`。重建版跑起來前這三件都要滿足 | [`program_info/cd_audio.md`](../program_info/cd_audio.md) |
| 連上前作的 `ailv3.lib` 不等於 AIL 齊了 | 16 個 `ail` function 在前作的庫裡完全沒有對應，其中 8 個有呼叫端，主體是 `0003ccf8` 領頭的 LX 驅動映像載入層。照直覺「AIL 沿用前作、不用管」會在連結時留下解不掉的外部符號 | [`program_info/code_pools.md`](../program_info/code_pools.md) |
| AIL 會反過來呼叫**遊戲自己寫的** DPMI 服務常式 | 把 `INT 31h` 的 `0100`／`0101`／`0600`／`0601` 包裝也算成 AIL 的一部分，等 `ailv3.lib` 提供。方向是庫以 EXTDEF 指向遊戲：這六支要由重建版自己定義並連進去，少了它們 AIL 的鎖頁與 DOS 記憶體配置全部解不掉 | [`program_info/code_pools.md`](../program_info/code_pools.md) |
| `00044dc0` 那 4 byte 的 `PUSHFD/POP EAX/CLI/RET` 屬於 AIL，不是 Watcom 的 `_disable` | 照抄前作 FD2 的 `crt.c`——它把這一段記成 `crt_equivalent_get_eflags` 收在 `crt` 裡。Watcom 真正的 `_disable` 是 `FA C3` 兩個 byte，這 4 byte 掃遍 10.0–10.6a 的 1,135 個 `.lib`／`.obj` 一次都沒出現；連同跳進它的 thunk `0003dcb0`，兩支都不能路由到 `crt` | [`program_info/code_pools.md`](../program_info/code_pools.md) |
| 影片播放不在重建範圍 | 三段過場由光碟上的 `FD.EXE` 播放，`FDPS.LE` 只負責 `spawnv` | [`program_info/cd_audio.md`](../program_info/cd_audio.md) |
| CD 音源在重建範圍內 | 選曲、起播、停止、循環全部由 `FDPS.LE` 自己下 MSCDEX 命令 | [`program_info/cd_audio.md`](../program_info/cd_audio.md) |
| 140 個章節／事件處理函式沒有任何直接呼叫者 | 只看呼叫圖會把它們當成死碼砍掉。它們全部只透過 `.object2` 的四張函式指標表被間接呼叫 | [`program_info/memory_layout.md`](../program_info/memory_layout.md) |
