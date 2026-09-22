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
| 記憶體管理的原版錯誤要照留：片尾名單每張卡片配置 89KB 卻只在迴圈外 free 一次、商店的移動網格在迴圈底部才配置而在頂部讀取（第一圈讀未初始化的堆疊、之後讀已 free 的區塊）、標題畫面的 demo 結束時 `free` 掉名冊區塊卻不重新配置也不清指標，而那塊是啟動時配一次、除了關閉流程沒有別人會釋放的區塊，所以跑過一次 demo 之後每一次名冊存取都落在已釋放的指標上 | 把 `free()` 移進迴圈、把配置提到迴圈外、把多餘的 `free()` 拿掉或在它後面補一句把指標設成 NULL。這些都只是「碰巧能跑」——Watcom 的近端堆積會把同尺寸的區塊原樣還回來，補了 NULL 反而把原版「照樣寫得進去」的行為換成空指標 | plate comment 的 `Rebuild note` |
| 越界讀寫踩到的是哪個鄰居，由堆積佈局決定，而堆積佈局只由 malloc／free 的呼叫序列與尺寸決定：物品 `FF` 讀到 `ITEM.DAT` 之後、單位陣列搬移多讀一筆、商店下箭頭寫出頁面、章節處理常式寫超過單位陣列尾端、`fdps_draw_text` 的 -6 數字把 '-' 變成字形 -3 讀到字型表前 0x60 byte，重建版要踩到同一批鄰居，前提是這個序列逐一保留 | 把 leak 補上 `free`、把重複載入（勝敗面板每開一次就重載九張資料表）提出迴圈、合併或調換配置順序、或把配置尺寸改成「正確值」。任何一項都會讓後面每個區塊換位置，這些越界存取從此落在不同的內容上，物品 `FF` 的數值、章節 24 狂戰士會不會認輸都跟著變 | [`program_info/memory_layout.md`](../program_info/memory_layout.md) |
| 浮動指示佇列的三個 200 byte 陣列與游標是一塊連續資料：`data_fdps_indicator_queue_cell_x_offset`（`0x64120`）、`data_fdps_battle_indicator_queue_unit_idx`（`0x641e8`）、`data_fdps_indicator_queue_glyph_ids`（`0x642b0`）、`data_fdps_indicator_queue_count`（`0x64378`），依序相鄰、中間沒有填充。游標沒有上限檢查，裂地術（`0x0A`）或封神裂震（`0x0B`）一次施放就能排進超過 50 個彈出視窗（MAP25 開場就有 52 個敵人落在同一個半徑 6 菱形與同一個畫面視窗內），第 201 格的寫入在原版落進下一個陣列，第三個陣列的溢位落在游標自己身上 | 把三個陣列與游標各自宣告成獨立的全域變數，由 linker 自己排。溢位會踩到別的東西，彈出視窗的內容與之後的游標值都跟原版不同。這四者要定義在同一個 object 裡、照原順序相鄰 | `src/indicat.c` 的註解 |
| 酒館的 1998-01-28 抽獎（`fdps_run_bonus_lottery`）用一個還沒寫入的堆疊槽決定發什麼獎。原版那一格固定落在 `fdps_transition_zoom` 存 EDI 的位置，值是 `fdps_run_bar_shop` 的一個堆疊位址，所以永遠發藥草；重建版讀到的是 `fdps_transition_zoom` 的 `camera_height_ramp[7]` = 1400，也落在藥草分支 | 兩邊一致是重建版 frame 排列的巧合。改了 `fdps_transition_zoom` 或 `fdps_run_bonus_lottery` 的區域變數（增刪、改型別、改宣告順序），或改了 `fdps_run_bar_shop` 在這兩次呼叫之間的呼叫序列，殘值就可能變成 0／1／2，把斬鐵劍、十個水晶粒或兩萬金變成拿得到。改動這三支之後，要用 WDISASM 確認抽獎讀的那一格仍然對到一個不等於 0、1、2 的值 | `src/vilbar.c` 的註解 |
| 鍵盤環形緩衝區沒有滿檢查，寫索引追上讀索引之後 `fdps_read_keyboard_queue` 回報空佇列，而裡面積著十個未讀掃描碼 | 加一個計數或滿檢查。改了之後遊戲收到的按鍵序列就不一樣 | plate comment 的 `Rebuild note` |
| 遊戲從來不呼叫 `srand`：`srand`（`00042d1a`）是 CRT 帶進來的孤兒碼，沒有任何 caller，種子從映像檔的初值 1 開始，所以每一輪遊戲的 `rand()` 序列一模一樣 | 在啟動時補一句 `srand(time(NULL))`，或以為種子在別處設過。命中、爆擊、連擊、異常狀態每一次判定的結果都由「在它之前總共呼叫過幾次 `rand()`」決定，補了種子等於把整個遊戲的隨機結果換掉 | plate comment（`00042d1a` 與 `00042cf2` 的 seed cell） |

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
| 固定字彙浮動指示的四個格子 x 偏移是 `i * 6 + 1`，但第 1 格單獨是 8 而不是 7，而且那是**位置**不是字距：`fdps_show_miss_indicator` 與 `fdps_show_cure_indicator` 是同一段程式，同一個第 1 格在 MISS 上是窄的 I、在 CURE 上是 U，一樣往右推 | 折成統一的 `i * 6 + 1`，或寫成「窄字母才往右推」的字距規則。前者把第 1 格往左移一像素，後者在 CURE 上根本不推 | plate comment 的 `Rebuild note` |
| 浮動指示回放的彈跳相位是**佇列索引** modulo 4，不是格子在自己那個彈出視窗裡的序號。`fdps_show_sprite_indicator` 只把游標推進實際寫入的格數，短標籤會讓游標停在非 4 的倍數上，之後排進來的每個彈出視窗於是整組相位錯開 | 寫成「每個彈出視窗的第 i 格延後 i 格」。看起來乾淨，而且游標剛好對齊 4 的倍數時兩者一模一樣，只有在短標籤之後才分岔 | plate comment 的 `Rebuild note` |
| 音效索引 `-1` 是活的輸入值：`fdps_audio_start_wav` 在沒有音效驅動、音效關閉或八格全忙時回 `-1`，升級視窗 `fdps_unit_award_exp_and_level_up`（`0001e328`）與 `fdps_run_bonus_lottery`（`00036973`）不檢查就交給 `fdps_audio_sample_is_playing` 空轉等待，原版於是讀到 handle 表 `data_fdps_audio_sample_handle_table`（`0x69d30`，8 個 dword）前面的 `0x69d2c`——一塊沒有任何引用、恆為 0 的填充，AIL 拿到 null handle 回「不在播放」，等待迴圈立刻結束 | 在程式碼補 `if (index < 0) return;`，或讓連結器自由擺放這張表。前者只有在確認過每個呼叫端之後才安全；後者讓 `table[-1]` 讀到重建版鄰居的值，AIL 可能讀出 4（等待迴圈卡死）或存取違規，沒有音效卡時一升級或一抽獎就當掉或卡死。程式碼照原樣不檢查，資料定義那邊讓表前面緊貼一個屬於同一個 object、初值為 0、永遠沒有人寫入的 dword | `src/audio.c` 的註解 |
| 名冊沒有人數上限：`fdps_roster_add_character` 直接拿 `data_fdps_roster_member_count` 當新成員的索引、寫入後無條件加一，`fdps_get_roster_record` 也不檢查索引；`0xa00` 區塊的 32 筆與存檔結構的 31 筆都是程式不強制的天花板 | 加入時補 `if (count >= 32) return;`，或在 accessor 裡夾索引。原版超過時照寫越界。村莊成員選單在測試人數之前就對六格全部取 record 指標，越界格只算出位址、從不讀取，把呼叫移進判斷式裡行為相同但沒有必要 | `src/` 各呼叫端的註解 |
| 「敵方陣營」不等於「肖像編號 ≥ `0x3C`」：`MAP%02d.DAT` 部署記錄的陣營 byte 與角色編號是兩個獨立欄位，`MAP12.DAT`（第 13 章）第 20 筆就是陣營 0、角色編號 `0D`、開場波次出場的單位，`MAP14`／`MAP18`／`MAP23` 另有陣營 0、編號 `23` 的單位。`fdps_combat_compute_hit_outcome` 與 `fdps_unit_apply_damage` 只看陣營就把「肖像編號 − `0x3C`」交給 `fdps_get_enemy_record`，對這種單位會讀到 `ENEMYDAT.DAT` 緩衝區前方的堆積記憶體當經驗值倍率；`fdps_unit_resolve_attack_hit` 與 `fdps_deploy_unit` 另外測了編號 | 把四處的判斷統一成「陣營 0 且編號 ≥ `0x3C`」，或在 accessor 裡加下界。兩者都會改變打這類單位時得到的經驗值；四個呼叫端各自的判斷要照原樣保留 | [`resource_info/map.md`](../resource_info/map.md) |
| WAV header 的解析結果被忽略，非 RIFF 的緩衝區會以未初始化的 14-byte 堆疊描述子播放出去；chunk 走訪也沒有 RIFF 的偶數對齊與邊界檢查 | 補上「解析失敗就回 -1」與正確的 RIFF 走訪 | plate comment 的 `Rebuild note` |
| `PROEQU.DAT` 的職業可裝備表是變長集合，用到的類型碼由小到大排在前面、空位填 `0xFF`，而 `fdps_unit_can_equip_item` **六格全掃、完全不測 sentinel** | 看到「變長集合」就補一個終止判斷。用 `0x00` 當終止值會提早收手——`0x00` 本身就是一個活的物品類型碼；用 `0xFF` 終止則會讓類型碼剛好是 `0xFF` 的物品不再撞上空位。原版兩種都不做 | [`assets/tables/classes.md`](../assets/tables/classes.md) |
| `SHOP%02d.DAT` 每列十二格的 `0xFF` 是**空位**不是列尾，兩者之間還會再有貨：`SHOP01.DAT` 的武器列是 `02 71 FF FF 72 73 FF …`、`SHOP03.DAT` 的是 `FF 03 FF 1E 1F 30 …`，`fdps_shop_collect_stock_items` 固定跑滿十二格、遇到就跳過 | 寫成 `for (i = 0; i < 12 && row[i] != 0xff; i++)`。多數商店的貨會被砍掉，`SHOP03.DAT` 那種第一格就是空位的整間店變成沒東西賣 | plate comment 的 `Rebuild note` |

## 不能換的型別與寫法

| 事項 | 照直覺會怎麼寫 | 正典 |
| --- | --- | --- |
| `.VFS` 成員查找是**單向**轉大寫：把傳入的名稱就地轉大寫，entry 名稱原樣取用，兩者 `strcmp` | 寫成 `stricmp(entry, query)`。遇到非全大寫的 entry 名稱行為就不同，而且原版會就地改寫呼叫端的緩衝區，這個副作用是可見的 | [`resource_info/vfs.md`](../resource_info/vfs.md) |
| `.VFS` 的 entry 筆數以 8-bit 讀入，第 256 筆以後走不到；entry table 偏移以帶號 16-bit seek，上限 `0x7FFF` | 用 `u32` 讀筆數、用 `long` seek。容器沒有踩到上限，但這是原版的硬限制 | [`resource_info/vfs.md`](../resource_info/vfs.md) |
| `SHOP%02d.DAT` 的商品編號列必須以 `unsigned char` 取值——原版是 `XOR EAX,EAX` / `MOV AL,[EDX]` 的零延伸，而 `0x80` 以上的編號是正常的貨（`SHOP01.DAT` 的道具列是 `B4 DE`） | 宣告成 `char *`。`0xFF` 變成 `-1`、跳過空位的判斷永遠不成立，空位會被當成物品編號 255 擺上架，`0x80` 以上的貨也全部變成負數編號 | plate comment 的 `Rebuild note` |
| 狀態視窗物品清單的游標環繞是拿 `count - 1` 比大小，不是取餘數，而那個 count 可以是 0：`fdps_unit_equip_window` 開這個清單之前不問單位身上有沒有東西 | 統一寫成 `*selected_slot = (*selected_slot + 1) % occupied_count`——存檔槽位的游標（`00024650`）確實是取餘數的，照著統一過來，空背包的單位一走進裝備畫面就除以零。原版讓索引走出 0..7，那種列不畫游標條，呼叫端等迴圈結束才呼叫 `fdps_unit_item_count` | plate comment 的 `Rebuild note`（`00025b20`） |
| `.SAF` 的 tilemap 格子編號是 `i16`（`short *` 取值、`-1 < index` 擋下界），但 layer 的 tilemap 編號是零延伸的 `u16` | 兩個都寫成同一種索引型別 | [`resource_info/saf.md`](../resource_info/saf.md) |
| `.SAF` 的 layer 半透明程度以 16-bit `MOVSX` 讀 `+0x07`，連 `+0x08` 的保留 byte 一起讀進來 | 宣告成 `u8`。保留 byte 恆為 0，所以目前無差別，但欄位的實際寬度是 2 | [`resource_info/saf.md`](../resource_info/saf.md) |
| 章節音軌表的位元組要 **+1** 才是 MSCDEX 音軌編號，加法由呼叫端在起播前做，不在表裡 | 直接把表值當音軌編號送出去，整首曲子會差一軌 | [`program_info/cd_audio.md`](../program_info/cd_audio.md) |
| CD 命令的 `INT 2Fh` 不是指令，是 DPMI `INT 31h` AX=0300h 的 real-mode call structure 裡的資料位元組 | 直接寫 `int 0x2f` 內嵌組語。在 DOS/4G 保護模式下走不通 | [`program_info/cd_audio.md`](../program_info/cd_audio.md) |
| `0x63930` 以後的 global 全部在 BSS，其中 `0x64000` 之後執行檔裡連內容都沒有 | 照 Ghidra 顯示的零值 emit 成初始化陣列。BSS 從 `0x63930` 起就該宣告成未初始化，而 `0x64000`–`0x6c3bf` 這 33KB 更是連檔案裡都不存在，載入器補的零與檔案帶的零長得一樣，照抄會把它們塞進映像檔 | [`program_info/memory_layout.md`](../program_info/memory_layout.md) |
| calling convention 要用 `#pragma aux <name> "*" parm caller [];` 宣告，不能用 `__cdecl` 關鍵字 | 照 C 的常識寫 `int __cdecl fdps_foo(int)`。Watcom 的 `__cdecl` 會在符號前面加一個底線，`-4s` 預設產生的是不加裝飾的名字——`_fdps_foo` 與 Ghidra 裡的 `fdps_foo` 對不起來，違反命名鐵則，而且連結時解不到別處對它的參照 | [`emit_pipeline.md`](emit_pipeline.md) |
| AIL 的每一條公開宣告都要掛 `#pragma aux AIL_<fn> "*" modify [eax ebx ecx edx];` | 照 C 的常識寫成 `extern void AIL_startup(void);` 就算。AIL 的 vendor object 不是 `wcc386` 的輸出，它會在沒有存回的情況下蓋掉 EBX／ECX／EDX；少了 modify 清單，編譯器會把活值留在 EBX 跨過 AIL 呼叫，值被無聲吃掉，沒有任何診斷，而且與該進入點宣告成 `__cdecl` 還是 `__watcall` 無關 | [`program_info/code_pools.md`](../program_info/code_pools.md) |
| `__sys_init_387_emulator`（`000444a4`）以 **EBP** 收一個活的旗標，跳進它的 `0003d50a` 把 EBP 原樣轉發過去 | 宣告成 `void __sys_init_387_emulator(void)`。任何 C 原型都表達不了這個介面，寫成 C 之後編譯器會自己配置 EBP，旗標就傳不進去 | [`program_info/code_pools.md`](../program_info/code_pools.md) |
| 執行期記錄一律要 `#pragma pack(1)`，不能用編譯器預設對齊 | 照 C 的常識宣告 struct，讓編譯器自己排。80x87 模擬器的 10-byte scratch operand 在預設對齊下會變成 12 byte，無聲蓋掉緊接在後的續行指標；同一個問題會出現在每一筆非 4 的倍數的記錄上，而且編得過、跑得動、值是錯的 | [`program_info/data_structures.md`](../program_info/data_structures.md) |
| 中斷處理常式進入時還沒載入 DS，對全域的檢查是 CS-relative 讀取（`CMP dword ptr CS:[0x605f2],0x0`） | 照 C 寫成對該全域的比較。編出來是 DS-relative，會去讀被中斷的那段程式碼的資料段。這種守衛必須留在組語裡或明寫 CS override | [`program_info/data_structures.md`](../program_info/data_structures.md) |
| RLE blit kernel（`00056a0d`–`00057a74`，mode 0–12）是手寫組語：沒有 prologue，以 ESI／EDI／EDX 交接參數，透過呼叫端的 EBP 讀第六個參數，`00057793` 與 `00057a74` 還把 `fdps_blit_dispatch`（`000568db`）的 `[EBP+8]`..`[EBP+0x1c]` 參數槽當暫存區蓋掉。但每支 kernel 唯一的呼叫端就是 dispatcher（mode 8 以 JMP 接到 `00056a0d`），dispatcher 在每個 CALL 之後直接跳到 POP／RET、不再讀自己的參數槽，46 個呼叫點也都在呼叫後立刻 `ADD ESP,0x1c` 丟棄，所以這些交接對外不可觀察：dispatcher 與 kernel **兩端同時**寫成一般堆疊慣例的 C、以明確參數交接，就是功能等價。只有保留一端的暫存器交接、另一端改成 C 時才需要 `.ASM` | 照 Ghidra 推出的 `void(void)` 或 `__watcall` 簽章寫，漏掉從 dispatcher frame 讀進來的第六個參數；或照其他 mode 的樣子統一參數表。參數必須依 dispatcher 實際放進暫存器與 frame 的內容來定：第六個參數在 mode 4／5 是就地拆成低半字與高半字（4 為寬／高無號，5 為 dx／dy 有號），在 mode 6 是指向四個 dword 槽的指標（各取低 16 位元：寬、高、dx、dy）；mode 4–8 不接受 dispatcher 算出的 `pitch − width` 列前進量，各自從 `0x0007002e` 的 pitch 推導，多傳一個前進量會讓鏡像與縮放的每一列錯位。另外 wcc386 10.0a 沒有敘述層級的內嵌組語（`_asm`／`__asm` 皆為 E1011），「內嵌組語」這條路在本工具鏈不存在 | [`program_info/code_pools.md`](../program_info/code_pools.md) |
| DGROUP 最上面的 8KB（`0x6a3c0` 以後）是堆疊段，不是 global | 看到 Ghidra 在那裡標了位址就當成 BSS 變數 emit。真正的 BSS 在 `0x6a3bc` 就結束了，那一段是堆疊與命令列、環境變數複本共用的空間 | [`program_info/memory_layout.md`](../program_info/memory_layout.md) |
| 同一個 16-bit 單位數值欄位（unit record `+0x40` HP、`+0x42` HP 上限、`+0x48` AP、`+0x4a` DP、`+0x4c` HIT、`+0x4e` EV）在不同 function 裡的延伸方式不同：戰鬥畫面的 `fdps_combat_compute_hit_outcome` 用 `XOR EAX,EAX` / `MOV AX` 零延伸，地圖上的 `fdps_unit_resolve_attack_hit` 用 `MOVSX` 符號延伸 | 結構裡宣告成 `short` 就直接讀，或為了統一改欄位型別。值為負時（例如 -1）原版戰鬥畫面讀到的是 65535（DP -1 等於不受傷、EV -1 等於永遠不被打中），直接讀得到 -1。零延伸的讀取點逐處寫 `(int)(unsigned short)`，欄位型別不動，因為符號延伸的讀取點同時存在。只拿來相加、結果再以 16-bit 寫回的讀取（`fdps_deploy_unit` 與 `fdps_roster_add_character` 對 `FRIAPRDA.DAT` 基礎值的讀取）無論哪種延伸寫出的 byte 都一樣，不需要轉型 | `src/combat.c`、`src/unitatk.c` 的註解 |
| 映像檔裡初值非零、而且程式在第一次讀取前從不寫入的 global（例：UI 調色盤循環相位 `0x60014` = 15、地形 HUD 面板欄位 `0x6016c` = `0x19`、音樂開關 `0x60008` 與戰鬥動畫開關 `0x60010` = 1），定義時必須帶上映像檔的初值 | 寫成沒有初值的定義。它落進 BSS、歸零，連結成功、測試也照過，但遊戲開場的畫面就與原版不同，而且沒有任何程式路徑會把它校正回來。`volatile` 同理：宣告帶了、定義也要帶，而定義所在的 `.c` 要 include 它自己的 `.h`，讓編譯器看得到兩者 | [`program_info/memory_layout.md`](../program_info/memory_layout.md)、[`data_emit.md`](data_emit.md) |
| 原版手寫的中斷處理常式第一道指令是 `STI`，以 `void __interrupt` 重建時做不到：wcc386 的 `__interrupt` prologue（pushad、四個段暫存器 push、建 frame、cld、call `__GETDS`）一定先跑，`STI` 最早只能落在 prologue 之後。這只延後 IRQ0 不到一微秒，被延後的 IRQ 由 8259 鎖存不會遺失，功能等價，不必為此改寫成組語 | 呼叫 `<i86.h>` 的 `_enable()`／`_disable()` 與 `<conio.h>` 的 `inp`／`outp`，以為它們是 intrinsic。本專案不開 `-oi`，它們只有在 `__INLINE_FUNCTIONS__` 下才是 intrinsic，否則編成對 CRT 的 CALL。`STI`／`CLI` 要用帶 body 的 `#pragma aux` 寫 | [`build_flags.md`](build_flags.md) |
| 清鍵盤緩衝的迴圈（如 `fdps_play_movie` @ `00030f40`、`src/cdaudio.c` 的 `while (kbhit()) getch()`）一律走 Watcom 10.0a CRT 的 `kbhit`（`00043570`）：先看 ungetch 旗標 `0x60440`，再以 `INT 21h` AH=0Bh 問 DOS 的 stdin 狀態，不直接讀 BIOS 鍵盤環形緩衝區 `0x41a`／`0x41c`，也不呼叫 `INT 16h` | 把 `kbhit` 換成直接比對 BIOS 緩衝區的 head／tail，或改用 `INT 16h` AH=01h。stdin 重導向、Ctrl-C 檢查與 ungetch 的語意都會不同。測試端的對應規則（DOS/4GW 在同一個 timer tick 內記住「沒有鍵」，手塞環形緩衝區後要先等 tick）見 [`emit_pipeline.md`](emit_pipeline.md) | `src/cdaudio.c` 的註解 |
| 存檔的 checksum 只加總 **`len - 4`** 個 byte，尾端 4 byte 的 checksum 欄位本身不算進去（`0x56898`） | 加總整個緩衝區。舊存檔一律驗不過 | [`program_info/code_pools.md`](../program_info/code_pools.md) |
| `FDE.SAV` 的 XOR 串流密鑰是硬寫的：DX 起始 `0xa5`，每個 byte 先 `DX += 0x9014` 再 `ROL DX,3`，取 DL 與資料 XOR（`0x568b7`） | 換一組看起來等價的常數或改變運算順序。加解密是同一支常式，改了之後新舊存檔互不相容 | [`program_info/code_pools.md`](../program_info/code_pools.md) |
| Watcom 的 `printf` 認得 `%hf`／`%hF`，那是 **16.16 定點數**轉換（吃 32-bit 整數、預設精度 4、完全不碰 FPU），不是 `%f` 的短版 | 轉錄格式字串時把 `%hf` 當成筆誤改成 `%f`。輸出數值會變，而且會把浮點格式化支援拉進映像檔 | [`program_info/code_pools.md`](../program_info/code_pools.md) |
| timer ISR 遞增的 tick 計數器 `0x69d64` 在每一支動畫的空轉迴圈裡都要宣告成 `volatile` | 當成普通 global 讀。最佳化器會把載入提到迴圈外，遊戲在第一個等待點就永遠停住 | plate comment 的 `Rebuild note` |
| 單位記錄的狀態 byte `+5` 有兩種寫法且不能互換：退場是**整個指派** `rec[5] = 1`，而「本回合已行動」是 `rec[5] \|= 0x80` | 統一寫成 `\|=` 與 `&= ~`。退場改成 OR 之後，已行動旗標會留在一個已經退場的單位上 | plate comment 的 `Rebuild note` |
| 單位記錄的 `+0x34` 是 packed byte：低 nibble 是行為模式，高 nibble 是別處會測的旗標，所以設模式一律是 `rec[0x34] = (rec[0x34] & 0xf0) \| mode` | 把它當成單純的模式欄位寫 `rec[0x34] = mode`，高 nibble 的旗標被無聲清掉 | plate comment 的 `Rebuild note` |
| 繪製順序與快照時機是行為的一部分：乾淨背景一律在內容畫上去**之前**取樣，地圖單位的影子全部畫完才畫第一個 sprite | 把「畫完再取樣」寫成比較自然的順序、或把兩趟掃描合併成一趟。前者讓每次重繪都疊上舊高亮，後者讓後面的單位把影子蓋到前面的單位身上 | plate comment 的 `Rebuild note` |
| VGA 的垂直歸線等待是**兩段式**：先等 `0x3da` bit 3 變 1，再等它變回 0。調色盤（DAC，port `0x3c8`／`0x3c9`）的寫入夾在兩段等待的**中間**，之後才搬畫面（`fdps_render_view_frame` 的 `fdps_cycle_scene_palette`、村莊看板的 `0xf0`..`0xf4` 循環都是這樣） | 只等 bit 3 變 1 就開始複製，每次複製會提早一個消隱期開始，轉場的樣子跟著變。或把兩段等待寫在一起當成一個「等 vsync」helper、再把 DAC 寫入放在它後面，DAC 就在顯示期間改色，畫面出現雪花或撕裂。兩段等待之間的工作要原樣留在中間 | plate comment 的 `Rebuild note` |
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
| `Icon%02d.dat` 的編號是**章節索引**（0 起算），而章節處理函式以玩家看到的章號命名，兩者差 1——`fdps_chapter_12_init` 載入的是 `Icon11.dat` | 照 function 名稱裡的章號寫檔名。二十九支會播到下一章的開場動畫，第 30 章更糟：`ICONANI.VFS` 只到 `ICON29.DAT`，找不到成員會停在 `fdps_wait_any_key` 等玩家按鍵，接著開場沒有 boss——`MAP29.DAT` 沒有 wave 0，這隻 boss 是動畫裡的 `DEPLOY_WAVE` 放的。而且照樣編譯照樣跑 | [`CONTEXT.md`](../CONTEXT.md) |
| `fdetxt%02d.txt` 的編號是**章節索引加 1**，也就是玩家看到的章號（`fdps_load_field_chapter_resources` 在 `00031563`..`00031568` 取 `data_fdps_chapter_current_chapter_id` 後 `INC EAX` 才 sprintf）；`FDETXT00.TXT` 是全域文字，由 `fdps_load_global_resources` 以固定檔名載入。這與上一列的 `Icon%02d.dat` 相反。證據：`FDETXT16.TXT` 第 0 筆是「第十六章」、`FDETXT30.TXT` 第 0 筆是「第卅章」 | 照 `Icon` 那條的經驗把文字檔也寫成章節索引，每一章都會講上一章的台詞 | [`resource_info/text.md`](../resource_info/text.md) |
| 地形修正表 `data_fdps_battle_tile_attr_ap_modifier_table`（`0x60040`）與 `..._def_modifier_table`（`0x60058`）宣告各 6 筆，但出貨地圖有 3222 格地形類別 6，三個讀取端（兩支戰鬥命中、游標資訊面板）都會讀到第 7 筆。原版靠資料段相鄰給出答案：AP 表 `[6]` 就是 DEF 表 `[0]`（常數 0），DEF 表 `[6]` 是 `0x60070` 的 dword，也就是 `data_fdps_village_mode_flag` 零擴展（高三 byte 為 0 且無人引用；旗標只在村莊商店迴圈內為 1，戰場上恆為 0） | 把三者各自定義成獨立物件，或把表加大成 7 筆填別的值，`[6]` 讀到的就是 linker 放在後面的東西。三者必須依序相鄰、各差 `0x18` byte，並在連結結果裡確認位移。旗標的初值是 0，照直覺會寫成沒有初值的 `char flag;`——那會落進 `_BSS`，離開兩張表所在的 `_DATA`，相鄰就斷了；它必須寫成 `= 0` 的帶初值定義。旗標後面三個 byte 不屬於任何符號，byte 比對的閘門看不到它們，連結結果改變時要重讀 DEF 表 `[6]` 那個 dword | [`data_emit.md`](data_emit.md)；`src/combat.c`、`src/unitatk.c`、`src/mapcur.c` 的註解 |
| 攻略站列出的失敗條件不一定有程式實作：第 14 章「蘭迪斯或法蓮娜死亡」只有蘭迪斯那一半存在——`fdps_chapter_14_post_action` 只轉呼共用判定（看 slot 0），`MAP13.DAT` 沒有任何一筆部署記錄代表法蓮娜，我方 slot 建立時死亡腳本一律是 `0xff`，章節 14 的四支處理函式都不看其他單位 | 照攻略或照第 4／5／6 章的形狀補一個 slot 3 的判定，就會多出原版沒有的敗北。章節的勝敗條件以處理函式與 `MAP%02d.DAT` 為準，攻略站只是線索 | `src/chpost1.c` 的註解 |
| `fdps_deploy_unit`（`000232b0`）的第三個參數：0 表示以錨點為中心搜尋最近的可站空格（跳過已佔用格、只接受地形碼 < 5 的格），非 0 才是直接放在錨點上（`00023360` 的 CMP／JNZ）。`src/` 的章節事件檔與 `tests/` 中有十五個名為 `*_PLACE_EXACT` 但值為 0 的巨集，名字與行為相反；傳入的值與原版相同，所以行為一致 | 照巨集名字去寫或改任何新的呼叫，會把兩種放置方式弄反。判斷語意一律看數值，不看名字 | `src/deploy.c` 的註解 |
| `0x640d8` 起的 0x20 byte 是**章節共用**的事件旗標陣列：`fdps_chapter_state_reset` 每次進章節整塊 memset，讀檔時整塊還原，十幾支不同章節的處理函式各自latch 其中一個 byte | 把它寫成該處理函式裡的 `static char done`。那是 assembly 看起來的樣子，但重來一章時不會被清掉，讀檔也還原不了 | plate comment 的 `Rebuild note` |
| CD 的 MSF 換算已經扣掉 150 frame 的 pregap：`fdps_cd_msf_to_sector` 回的是邏輯磁區號，`fdps_cd_sector_to_msf` 又再扣一次 150（因為它的輸入已經是扣過的） | 寫教科書版的 `minute*60*75 + second*75 + frame`。每一軌的起點都會差 150 frame，長度查詢則會少兩秒 | [`program_info/cd_audio.md`](../program_info/cd_audio.md) |
| **Ghidra 的字串定義常常早 1–2 byte 開始，把 Watcom 留在字面值之間的非零對齊填充算進去**：`s_Icon03.dat_00061827` 的值是 `"zIcon03.dat"`，反編譯印出的是 `fdps_icon_script_run(s_zIcon03_dat_00061827 + 1)` | 把反編譯印的字串原文抄進 C。抄到的是多了填充字元的字串，`fdps_vfs_find_entry` 一定找不到那個成員。這在 `.object2` 的字串區反覆發生，不是個案；判斷方式是看呼叫端有沒有 `+ 1` / `+ 2` 這種偏移 | [`program_info/data_structures.md`](../program_info/data_structures.md) |
| **調色盤循環表刻意把開頭幾筆複製到尾巴**，例如 24 筆 = 16 筆再接前 8 筆、29 byte = 15 筆再接前 14 筆，好讓 `base + phase` 這種讀法不必做環繞測試 | 看出重複就把尾巴刪掉、改寫成 `(phase + i) & 15`。要嘛脈動序列不同、要嘛直接讀出界，而兩者都不會有任何診斷 | plate comment 的 `Rebuild note` |
| **`.object1` 裡夾在函式之間的常數表，多半是 wcc386 替 auto 陣列產生的初值影像，不是原始碼裡的全域**：`int cmd_icons[4] = {0x16, 0x0b, 0x0c, 0x13};` 這樣的區域宣告，初值會被擺在宣告它的函式旁邊的唯讀資料裡 | 看到有名字的常數表就當成全域 emit 出來。原版沒有那個全域，重建版多一個符號、而且該函式每次進入時的複製動作不見了。判定過的 1,040 個 anchor 裡有 395 個屬於這一類 | [`program_info/data_structures.md`](../program_info/data_structures.md) |
| 同一個陷阱還有另外三種產生者：**`switch` 的跳躍表、浮點常數池、字串字面值**，Ghidra 一樣會給它們名字 | 把 `binary_artifact_*_switch_table_*`、`binary_artifact_fp_const_*`、`binary_artifact_string_literal_*` 當全域 emit。編譯器本來就會從 `switch`、從算式裡的浮點字面值、從敘述裡的字串重新產生一份，我們再定義一次就是原版沒有的第二份。這三類加上區域陣列初值共 159 個符號，已經在 routing 表裡標成不 emit | [`code_layout.md`](code_layout.md) |
| 三十支章節 init 處理函式看起來一模一樣，但**不能用迴圈或樣板生成**：`Icon%02d.dat` 的編號差 1、`fdps_roster_add_character` 必須排在 `fdps_chapter_state_reset` 之前（reset 會依名冊人數重建地圖單位，順序反過來新加入的角色會被歸零成退場）、而且第 17／22／23 章傳的游標目標不是 0 | 用一支樣板產生三十支。前兩項會讓某些章節少一個角色或播錯動畫，第三項只影響三章 | plate comment 的 `Rebuild note` |

## 不能照編譯器慣例設定的旗標

旗標組本身與判定依據見 [`build_flags.md`](build_flags.md)，這裡只收「不照原版設會出事」的各項。

| 事項 | 照直覺會怎麼寫 | 正典 |
| --- | --- | --- |
| 程式庫要用 10.0a 的，10.0 家族的三個發行版不能互換 | 手上裝了哪個 10.0 就連哪個，反正都是 10.0 家族。10.0b 的 `MATH387S.LIB` 把 `strtod` 重編成大 4 byte 的框架，又把 5 byte 的裸 `IF@TAN` 換成 21 byte、會回退到軟體實作的守衛版；10.0 的 `CLIB3S.LIB` 則有另一套 `__prtf`／`__scnf`／`__isindst`／`_nmalloc`。連錯版本不會有任何診斷，映像檔就是另一份 | [`build_flags.md`](build_flags.md) |
| 遊戲模組用 `-s` 關掉堆疊檢查 | 不加 `-s`，用編譯器預設。預設會在每個有框架的 function 前插入 `push <大小>` / `call __CHK`——原版的遊戲碼在堆疊耗盡時是直接寫穿，重建版會改成印 `Stack Overflow!` 然後結束，外顯行為不同。`__CHK`（`0x4361a`）的 34 個直接呼叫端中，32 個是 CD 模組 `fdps_cd*`、2 個是 CRT 的 `spawnvpe`（`0x54dcc`）與 `spawnve`（`0x556ae`）；原版 CD 模組是以 `-os`、不帶 `-s` 編的所以帶 probe（重建版的建置目前還沒跟上，由票 22.2 對齊），CRT 那兩支則照程式庫原樣帶檢查 | [`build_flags.md`](build_flags.md) |
| `-ot` 要寫在 `-d2` 前面 | 把 `-ot` 擺在最後，或只寫 `-d2`。`wcc386` 由左而右處理選項：`-ot` 設定「以速度為優先」的偏好，`-d2` 之後才關掉最佳化器而不清掉那個偏好。`-ot` 擺到 `-d2` 之後會把最佳化器重新打開，區域變數不再來回堆疊、序幕與 switch 表全變（42 支真實 function 的逐 byte 對照從 17 支相同掉到 0 支）；不寫 `-ot` 則所有位址縮放從 `lea reg,[reg*N]` 變成 `shl reg,N`（原版有 304 處） | [`build_flags.md`](build_flags.md) |
| 原版是 `-d2` 不是 `-od`，而且 `-oe=25` 的門檻要明寫 | 用 `-od` 關最佳化，或 `-oe` 不帶門檻。`-od` 會把宣告的變數當引數時直接推（原版經 EAX 中轉）、函式表呼叫換成 EAX 索引；預設的 `-oe` 門檻展開不到原版展開的幾支，門檻 27 以上又會多展開原版保持呼叫的。LE header 的 `debug_info_off` 為 0 看起來像是「沒帶除錯資訊」，但 wlink 從不填那個欄位，不能拿來排除 `-d2` | [`build_flags.md`](build_flags.md) |
| 10.0a 的 C 沒有 inline 關鍵字，映像裡的展開是 `-oe` 做的 | 讀到 function 本體出現在呼叫端裡，就推論原始碼寫了 `_inline`，重建時照寫。`_inline`／`__inline`／`inline` 在 `wcc386` 10.0a 全部是 E1009 語法錯誤；展開只發生在同一個 translation unit 內，所以原始碼的分檔決定哪些呼叫會被展開 | [`build_flags.md`](build_flags.md) |
| 原版用 `-fpi` 而不是 `-fpi87` | 沿用前作 FD2 的 `-fpi87`。wlink 只抽出解得掉未定義符號的 lib 成員，`-fpi87` 不會發出 `__init_387_emulator` 這個參照，於是 `emu387.lib` 就算在 `.lnk` 裡列了也不會被連進去——在沒有 387 的環境下遊戲的浮點運算直接當掉 | [`build_flags.md`](build_flags.md) |
| 連結要明寫 `option stack=8k` | 不寫，讓 wlink 用預設。wlink 的預設是 4K，只有原版的一半 | [`build_flags.md`](build_flags.md) |
| 原版用 flat 記憶體模型（`-mf`），const 資料與區域陣列初值影像因此落在程式碼 object | 用 `-ms`。除了資料搬家到 DGROUP 之外，每次把初值複製到堆疊前還會多兩條 `mov ax,ss` / `mov es,ax`。反過來說，讀 Ghidra 時看到常數表夾在函式之間也不要當成分析錯誤 | [`build_flags.md`](build_flags.md) |

## 環境與範圍

| 事項 | 內容 | 正典 |
| --- | --- | --- |
| 本機 `WATCOM_10.0a` 的 `lib386\dos\clib3s.lib` 是殘缺的副本 | 它比同一發行版的完整副本少一個模組（`stk386`，393 對 394），少掉的正是 `__CHK`／`__STK`／`__GRO`／`__STKOVERFLOW` 這組堆疊檢查 stub。遊戲模組用 `-s` 不會參照它們，但帶堆疊檢查的 CRT 模組（`spawnvpe`、`spawnve` 呼叫 `__CHK`）會，連結時就是解不掉的外部符號。建置與比對都要改用 `WATCOM_10.0a_infobase` 那份，或先把檔案補回去——兩份安裝共有的 393 個模組 SHA-256 全數相同，其餘四個程式庫也是模組對模組、雜湊對雜湊一致，所以換過去不改變任何結論 | [`build_flags.md`](build_flags.md) |
| 10.0a 的 DOS 版 `wcc386` 與 `wlink` 不在同一個 bin 目錄 | `wcc386` 在 `BINB\`、`wlink` 在 `BIN\`；9.5 家族兩者都在 `BIN`、10.5 之後在 `BINW`。認定工具都在 `BIN\`（或沿用前作 FD2 硬寫的 `D:\BIN\WCC386.EXE`）在 10.0a 上直接找不到編譯器。三個目錄都放進 guest 的 `PATH`、工具以裸名呼叫 | [`build_pipeline.md`](build_pipeline.md) |
| DOSBox-X 的離開碼一律是 0 | `IMGMOUNT`、編譯器、連結器全部失敗它也回 0，所以 `subprocess` 的回傳值不能當判準。建置看產物存在與未解符號，執行看程式自己寫出來的結果檔，光碟掛載則只有真的讀出磁碟上的位元組才算數 | [`build_pipeline.md`](build_pipeline.md) |
| 要驗證音效就不能用 DOSBox-X 的 `-silent` | 為了全自動化，一律加 `-silent`。它連 Sound Blaster 的模擬一起關掉：驅動程式探測不到硬體，`AIL_install_DIG_INI` 回 NULL，而 `AIL_get_last_error_code` 是 **0**——看起來像沒發生錯誤，實際上什麼都沒裝起來。要碰音效的那一段改成不加 `-silent`（會開視窗，但仍然自己跑完自己退出），其餘照舊 | [`ail_link.md`](ail_link.md) |
| 要掃 DOSBox-X 的 log 找保護模式故障，就得在 conf 裡指定 `[log] logfile=` | 只捕捉 stdout 拿到的是幾行初始化訊息加上一句「No logfile was given. All further logging will be discarded」。掃描於是永遠掃到空的、永遠回報沒有故障，而這件事在成功路徑上完全看不出來。log 是附加寫入，每次跑之前還要刪掉舊的 | [`build_pipeline.md`](build_pipeline.md) |
| 編譯旗標要走 `WCC386` 環境變數，不能展開在批次檔的呼叫行上 | COMMAND.COM 的命令列在變數展開後超過約 176 字元會**靜默截斷**，最先被吃掉的是排在最後的 `-fo=` 目的檔路徑。寫成 `wcc386 %CF% ... -fo=<路徑>` 會在旗標一長就無聲壞掉 | [`build_pipeline.md`](build_pipeline.md) |
| build gate 過了不代表 fixup 還指向原本的符號 | 把「重定位感知的比對 PASS」當成語意沒變。它在比對前會把每個 fixup site 的值與整張 Fixup Record Table 抹零，所以「換掉某個 fixup 指向的符號」剛好完全落在盲區裡——那種改動只動到 site 的位移值與該筆 record 的 target 欄位，判定會是通過的 `reloc`，但讀到的已經是另一個符號。前作就是這樣讓復活價格讀成了鄰居那張對話 id 表。要驗證「指向哪個符號」得反組譯 `.obj` 直接讀 fixup 的符號名，閘門給不了這個答案 | [`build_gate.md`](build_gate.md) |
| 啟動的三道光碟檢查 | `access("DISK.NO")`、由 `Disk.no` 第三個 token 取得路徑前綴、MSCDEX 安裝檢查，任一不過就 `exit(1)`。重建版跑起來前這三件都要滿足 | [`program_info/cd_audio.md`](../program_info/cd_audio.md) |
| AIL 會反過來呼叫**遊戲自己寫的** DPMI 服務常式 | 把 `INT 31h` 的 `0100`／`0101`／`0600`／`0601` 包裝也算成 AIL 的一部分，等 `ailv3.lib` 提供。方向是庫以 EXTDEF 指向遊戲：這六支要由重建版自己定義並連進去，少了它們 AIL 的鎖頁與 DOS 記憶體配置全部解不掉 | [`ail_link.md`](ail_link.md) |
| `ailv3.lib` 的 EXTDEF 用的是**前作**對那七個遊戲側符號的拼法 | 在 `src/` 裡照 `ailv3.lib` 的名字定義 `fd2_dpmi_lock_size` 之類的東西，或反過來把 FDPS 的符號改名遷就庫。兩種都會讓 C 名稱與 Ghidra 名稱對不上，違反 [`naming.md`](naming.md) 的鐵則。接點放在 `.lnk` 的 `alias` 指令 | [`ail_link.md`](ail_link.md) |
| 前作的庫沒有的那 16 個 `ail` function 裡，**只有 4 個要補、另外 12 個不必** | 看到「16 個沒有對應」就整批去重抽或手寫。要補的是 `00044dc0`（存 EFLAGS 的 4 byte）與三支已改判成遊戲程式碼的 DPMI 常式——少了它們連結解不掉。另外 12 個不必補：主體的 LX 驅動映像載入層（`0003ccf8` 領頭）在 `FDPS.LE` 裡本來就是連結器整包抽進來的死碼，沒有任何可達的呼叫端，`ailv3.lib` 也沒有參照它們 | [`ail_link.md`](ail_link.md) |
| `00044dc0` 那 4 byte 的 `PUSHFD/POP EAX/CLI/RET` 屬於 AIL，不是 Watcom 的 `_disable` | 照抄前作 FD2 的 `crt.c`——它把這一段記成 `crt_equivalent_get_eflags` 收在 `crt` 裡。Watcom 真正的 `_disable` 是 `FA C3` 兩個 byte，這 4 byte 掃遍 10.0–10.6a 的 1,135 個 `.lib`／`.obj` 一次都沒出現；連同跳進它的 thunk `0003dcb0`，兩支都不能路由到 `crt` | [`program_info/code_pools.md`](../program_info/code_pools.md) |
| 影片播放不在重建範圍 | 三段過場由光碟上的 `FD.EXE` 播放，`FDPS.LE` 只負責 `spawnv` | [`program_info/cd_audio.md`](../program_info/cd_audio.md) |
| CD 音源在重建範圍內 | 選曲、起播、停止、循環全部由 `FDPS.LE` 自己下 MSCDEX 命令 | [`program_info/cd_audio.md`](../program_info/cd_audio.md) |
| 140 個章節／事件處理函式沒有任何直接呼叫者 | 只看呼叫圖會把它們當成死碼砍掉。它們全部只透過 `.object2` 的四張函式指標表被間接呼叫 | [`program_info/memory_layout.md`](../program_info/memory_layout.md) |
