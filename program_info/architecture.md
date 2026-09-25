# 程式架構總覽

**驗證對象**：`FDPS.LE` 的執行骨幹——從 LE 進入點到 `main`、`main` 的頂層迴圈與分派方式、各子系統的進入點、呼叫圖的整體形狀（可達性、共用 helper、遞迴環）。「程式是怎麼組起來的」以此檔為唯一正典；個別子系統的內部細節由該子系統的正典檔擁有，位址空間與指標表由 [`memory_layout.md`](memory_layout.md) 擁有。

## 涵蓋範圍

Ghidra 認得 1,345 個 function。呼叫與跳躍邊合計 3,741 條；從 LE 進入點 `00043298` 與 325 個被函式指標表指到的進入點一起出發，能走到 1,008 個 function，最深 11 層。

遊戲本體那 514 個 function（`pool_fdps`）已經逐一讀過 assembly 並定案：名稱、calling convention、每個參數的語意名稱與描述行為的 plate comment 都在 Ghidra 裡，沒有預設命名殘留。子系統分佈：

| 子系統 | 數量 | 子系統 | 數量 |
| --- | ---: | --- | ---: |
| battle | 189 | cd | 27 |
| script | 52 | file_io | 8 |
| graphics | 50 | input／audio／memory | 各 6 |
| menu | 40 | save | 3 |
| field | 30 | startup／main_loop／string | 各 1–2 |

calling convention 是 `__cdecl` 503 個、`__watcall` 11 個。`__watcall` 那 11 個全部是遊戲自己寫的組合語言，不是 `wcc386` 的產物——`wcc386 -4s` 建置的 C 程式碼一律走堆疊慣例，Ghidra 全 binary 標成 `__watcall` 是自動分析的預設值而不是事實（見 [`rebuild_info/build_flags.md`](../rebuild_info/build_flags.md)）。

其餘 831 個是 vendor 程式碼（`crt` 與 `ail`），命名依 [`rebuild_info/naming.md`](../rebuild_info/naming.md) 只用程式庫原名，判不出 PUBDEF 的就保留預設名稱，不硬湊。

全域資料的名稱、型別與 struct 佈局由 [`data_structures.md`](data_structures.md) 擁有。不少 plate comment 仍以 `DAT_xxxxxxxx` 的位址寫法指稱全域資料，那只是位址記法，符號的名稱以 Ghidra 的 label 為準。

## 啟動鏈

LE 進入點到 `main` 是一條五段的固定鏈，全部是 Watcom C/C++ 10.0a 的 runtime，沒有遊戲自己的程式碼：

| 位址 | 身分 | 作用 |
| --- | --- | --- |
| `00043298` | `_entry` | 單一 `JMP`，跳過連結器嵌在中間的版權橫幅資料 |
| `00043310` | `_cstart_`（`cstrt386.asm`，手寫組語） | 偵測 DOS extender 主機、縮記憶體、建命令列與程式名、歸零 BSS |
| `0004dfa6` | `__InitRtns` | 以優先權 255 走過連結器建的 XI initialiser 表 |
| `0004df4c` | `__CMain` | 保留 auto-stack、跑 `__CommonInit`、呼叫 `main`、把回傳值交給 `exit` |
| `00029220` | `main` | 遊戲的 C 進入點 |

`00043310` 的主機偵測分三路：`'DX'` 是 Phar Lap 386|DOS-Extender、`'CB'` 是 Intel Code Builder、其餘走 DPMI（`INT 21h` AX=FF00h、`INT 31h` AX=0006h），也就是本作實際使用的 DOS/4G 路徑。DPMI 路徑下 BSS 歸零只清 `0x1000` byte，因為載入器已經補零。`00043310` 還有一個自我修改步驟：把 DS 寫進 `0004356d` 的指令運算元，供離場路徑 `00043564` 使用。

`__CMain` 之後控制權不再回到啟動碼。`0004df4c` 呼叫 `00029220`，再把回傳值交給 `exit`（`00042e0f`），`exit` 跑完 finalizer 鏈後由 `INT 21h` AH=4Ch 結束程式。`__ASTACKSIZ`（`00060438`）在這個 build 是 0，所以 `__CMain` 的 auto-stack 區塊實際上不配置。

## main 做的事

`main`（`00029220`）依序做六件事：

1. **安裝檢查與光碟檢查**：`access("DISK.NO", 0)`、讀 `Disk.no` 取第三個 token 當光碟路徑前綴、呼叫 `0003c636` 偵測 MSCDEX。任何一項失敗就印訊息並 `exit(1)`。細節見 [`cd_audio.md`](cd_audio.md)。
2. **子系統啟動**：`000304e0`（音效，帶參數 `0x19`）、`00029660`（全域資源），然後 `int386(0x10, ...)` 以 AX=0x13 進 VGA 320×200×256。
3. **狀態歸零**：清 `00069cf0`、`00069cdc`、`00069da0` 與離場旗標 `000643eb`。
4. **開場**：呼叫一次 `0002a2b0`（標題畫面）。
5. **頂層迴圈**：見下節。
6. **收尾**：`00029440`（釋放全域配置）、`000305a0`（關閉音效，內容是單一 `AIL_shutdown` 呼叫）、`0003c4a7`（停止 CD 音軌）、`int386(0x10, ...)` AX=3 回文字模式，最後印 `Thank you for playing Flame Dragon Plus!!`。

`main` 沒有 `return` 敘述，控制權落到函式尾端，EAX 是最後一次印訊息留下的值，`__CMain` 把它交給 `exit`。

## 頂層迴圈的形狀

迴圈條件是離場旗標 `000643eb` 為零。每一圈只有兩步：

```
while (quit_flag == 0) {
    fdps_battle_player_phase_loop();      /* 0002bae0 */
    switch (request_code /* 00069da0 */) {
        case 0: break;
        case 1: fdps_show_game_over();    /* 0002a960 */
                fdps_title_screen();      /* 0002a2b0 */
                break;
        case 2: table_00060304[chapter_id /* 00069cf4 */]();
                fdps_run_village_phase(); /* 00031210 */
                break;
    }
    request_code = 0;
}
```

也就是說**整個遊戲的主迴圈就是「跑一次戰鬥的玩家階段，然後看這一階段留下什麼請求碼」**。請求碼 `00069da0` 是戰鬥子系統與頂層迴圈之間唯一的溝通管道：0 表示戰鬥還沒結束（下一圈繼續同一場戰鬥的玩家階段）、1 表示全滅（Game Over 後回標題）、2 表示章節通關（跑該章的劇本進入點，再進村莊階段）。

分派用兩張函式指標表，都不會被任何直接 `CALL` 指到：

- `00060304` 由 `main` 以章節編號 `00069cf4` 索引，是章節通關後的劇本進入點。
- `00060074` 由 `0002a2b0` 與 `00031210` 索引，是章節開始的進入點；entry stub 排在 `00020e90`–`00021650` 的等距區塊，每一個都以同一條重置尾巴 `00022750` 結束。

表的 entry 數與呼叫端指令位址由 [`memory_layout.md`](memory_layout.md) 擁有。

戰鬥內部另有一層自己的迴圈：`0002bae0` 逐格輪詢鍵盤、捲動地圖、把選取交給指令選單，直到階段結束旗標被設起；回合推進（休息回血、我方 NPC 階段、敵方階段、回合數加一、下一個玩家階段）集中在 `0001e3f0`，由自動結束（`0002ea10`）與手動結束（`00014ab0`）兩條路徑呼叫。

## 各子系統的進入點

| 子系統 | 進入點 | 作用 |
| --- | --- | --- |
| startup | `00029220` | `main` |
| | `00029660` | 一次性開啟 `Misc.vfs`／`Field.vfs`，快取所有全域 palette／cel／字型／文字資源，建 Bar 條帶與 palette 合併表 |
| | `00029440` | 離場時釋放全部全域配置 |
| main_loop | `00022750` | 每個章節進入時共用的狀態重置尾巴 |
| | `0002ac10` | 標題畫面待機超時後的 attract demo |
| menu | `0002a2b0` | 標題畫面與開場 attract 循環，回傳選單索引給 `main` |
| | `00031210` | 章節之間的村莊階段：五個設施選單，出來後跑該章劇本 |
| battle | `0002bae0` | 玩家階段的地圖互動迴圈 |
| | `0001e3f0` | 回合推進：休息、我方 NPC、敵方、下一回合 |
| | `00015470` | 單一我方單位的完整互動回合（移動範圍、目的地選擇、指令選單） |
| graphics | `0002beb0` | 一幀畫面的組合與呈現：配 86,400 byte 場景緩衝、兩個 helper 填內容、等垂直歸位、把 312×192 視窗貼到 `000a0504`（畫面第 4 列第 4 行），再等一個 timer tick |
| | `000568db` | 13 種模式的 blit 分派器，所有 sprite 繪製的共同出口 |
| audio | `000304e0` | Miles AIL 啟動：裝 MDI 與 DIG 驅動、配 1 個 sequence handle 與 8 個 sample handle |
| | `000305a0` | 離場時關閉 AIL，與 `000304e0` 對稱 |
| | `0002a1f0` | 依名稱播放音效 |
| cd | `0003c636` | 啟動時的 MSCDEX 偵測 |
| | `00030cc0` | 換片檢查與章節音軌起播 |
| input | `000567f0` | 保存並掛上 `INT 09h`（IRQ1）向量 |
| | `00056837` | 鍵盤中斷處理常式：讀 port 0x60、記錄 scancode、推進 10 格環形緩衝、發 EOI |
| file_io | `00039ab0` | 開啟 `.VFS` 容器，把目錄表讀成記憶體 handle |
| | `0002a140` | 從 VFS 取出具名 entry 到新配置的區塊，失敗就中止程式 |
| | `00018930` | 載入九張全域資料表（`Friaprda.dat`、`FriLevUp.dat`、`Item.dat`、`EnemyDat.dat`、`ProMap.dat`、`ProEqu.dat`、`MagicDat.dat`、`GetMgTab.dat`、`RankUp.dat`） |
| | `00023e20` | 讀 `FDE.SAV`、驗 checksum、重建所有全域遊戲狀態 |
| memory | `0003d375` / `0003d478` | Watcom near-heap 的 `malloc` / `free` |
| | `00018a20` | 釋放九個全域資源緩衝 |
| string | `000435a2` | Watcom `sprintf` |

音效與繪圖是唯二有兩個平行進入點的子系統：一個是啟動時的一次性建置，另一個是執行期每幀都會走的路徑。

## 切工作時不能拆開的共用群集

有 75 個 function 各被 8 個以上不同的呼叫端使用。它們是分區的天然邊界——任何把工作切成多份的做法都必須讓每一組完整落在同一個分區裡，否則型別、struct 與慣例會在分區之間分歧。

**單位記錄存取器 `0002d210`（141 個呼叫端）是全程式被呼叫最多的 function**，它只是回傳 `00069cd8` 那個 0x50-byte 陣列的第 n 個元素。它一被定型，整個 battle 子系統的 struct 就跟著定型；反過來說，它不能屬於任何單一分區。

分成五組：

| 群集 | 成員（呼叫端數） |
| --- | --- |
| 戰鬥單位存取 | `0002d210`（141）、`000109b0`（34）、`00018b40`（25）、`00018b70`（11）、`00018bd0`（11）、`00034520`（12）、`00027840`（8） |
| 繪圖原語 | `0002f2f0` blit_rect（48）、`0002beb0` render_view_frame（41）、`0002dba0` cel_blit_sprite（34）、`000568db` blit_dispatch（27）、`0002bf60`（21）、`0002d7c0`（19）、`00014140`（18）、`00022f40`（16）、`00014550`（12） |
| 資源與狀態 | `0002a140` vfs_load_entry（30）、`00022750` chapter_state_reset（32）、`00023830`（34）、`00023980`（30）、`00023bc0`（12）、`0002ba00`（15）、`0002a1f0` play_sfx（24）、`00030c50`（8） |
| 大型共用 helper | `0001ff60`（75 個呼叫端、1,134 byte）、`00021650`（62、1,981 byte）、`00039e70`（29、1,123 byte）、`00020c60`（30）、`000205b0`（14）、`00020820`（14）、`00017530`（13）、`00025d20`（21）、`00015be0`（9） |
| vendor library 邊界 | `0003d478` free（96）、`0003d375` malloc（90）、`00042cd0`（50）、`0003d4e4`（46）、`0003d514`（46）、`00045183`（42，AIL log）、`00044f42`（41）、`0004265e` fopen（22）、`000428be` fclose（22）、`00042e0f` exit（11） |

vendor library 那一組不該進任何遊戲分區——它們由 vendor 契約處理，見 [`rebuild_info/build_flags.md`](../rebuild_info/build_flags.md)。

另有五個遞迴環，同樣必須整組留在一起：

| 大小 | 成員 |
| --- | --- |
| 10 | `00010010`、`00012c10`、`00012e50`、`00013c90`、`0001d990`、`00021650`、`000262a0`、`00027180`、`000372d0`、`000374e0` |
| 7 | `00041eb5`、`00041f9a`、`000486a0`、`00048bc0`、`00048c70`、`0004ac10`、`0004adb0` |
| 5 | `0004ec3c`、`0004edde`、`0004ee30`、`0004f770`、`0004fe2f` |
| 4 | `00051e12`、`0005241e`、`0005250f`、`0005254b` |
| 2 | `000556a9`、`00056502` |

最大的那個環橫跨戰鬥 AI 與地圖行為，且與共用 helper `00021650` 重疊——這是整個 fdps 池裡最不能分割的一塊。

## 呼叫圖走不到的部分

337 個 function 走不到，按池分是 `ail` 203、`crt` 98、`fdps` 36。它們散成 153 個弱連通群集，其中 95 個是完全沒有進出邊的孤點；最大的六個是：

| 大小 | 位址範圍 |
| --- | --- |
| 58 | `0003de38`–`00054852` |
| 24 | `0004bde5`–`0005670a` |
| 23 | `0003ed8f`–`0004bc99` |
| 10 | `0003f2b9`–`0004b720` |
| 8 | `0003d50a`–`00054f51` |
| 7 | `0003dcb0`–`00054564` |

群集全部落在 `0003c000` 以上的程式庫區。每一個要嘛是連結器帶進來的死碼，要嘛唯一的入邊是尚未解出的間接呼叫——AIL 在執行期以驅動映像的內部表分派，那些邊靜態看不到。

走不到的 36 個 `fdps` function 沒有一個在章節分派表裡——四張表的 140 項在 `src/chapter.c` 全部是具名初值，呼叫圖經由各表的分派者走得到每一項。它們分成三類：

| 類別 | 數量 | 成員 |
| --- | ---: | --- |
| `-oe` 展開後留下的本體：呼叫點全部被編譯器就地展開，獨立的那一份沒人呼叫 | 7 | `fdps_move_grid_set_stop_flag`、`fdps_unit_mark_retired`、`fdps_draw_gauge_bar_proportional`、`fdps_draw_stat_gauge`、`fdps_draw_unit_gauge_proportional`、`fdps_spell_heal_unit`、`fdps_pack_rgb` |
| 以位址掛上的中斷處理常式：執行期會跑，但入口是寫進中斷向量或 AIL timer callback 的位址，不是 `CALL` 也不在指標表裡 | 2 | `fdps_keyboard_isr`、`fdps_timer_tick_handler` |
| 完全沒有呼叫者的成員 | 27 | MSCDEX 包裝的 19 支（`src/cd.c`、`cdtoc.c`、`cdaudio.c`；其中 `fdps_cd_get_track_length_sectors`、`fdps_cd_ioctl_output_command`、`fdps_cd_read_q_channel` 只被同樣沒人用的成員呼叫）、VFS 讀取器的 3 支（`src/vfs.c`）、前作 `.DAT` 封裝檔的讀取器 `fdps_load_indexed_archive_entry`（FDPS 沒有這種檔）、兩種沒用上的全螢幕轉場 `fdps_transition_box`／`fdps_transition_slide`、沒接上的 raw PCM 播放介面 `fdps_audio_start_sample`／`fdps_audio_set_sample_playback_rate` |

前兩類不是死碼：第一類的行為以展開後的形式活在呼叫端裡，第二類在執行期被硬體中斷或 AIL 呼叫。第三類各自屬於刪減與未用的哪一類（[`CONTEXT.md`](../CONTEXT.md)），逐條的判定與證據由 [`cut_content/`](../cut_content/_index.md) 擁有。

## 尚未確定的事

**貼圖模式的語意已定**。`fdps_blit_dispatch`（`000568db`）底下的 13 個 kernel 各自對應一種模式：原樣、調色盤重映射（分「只映射 sprite」與「連背景一起」兩種）、換色、縮放、旋轉、旋轉加縮放、水平鏡像、垂直鏡像、半透明、半透明限定色域、著色、著色連背景。它們全部是手寫組合語言，但交接封閉在 dispatcher 之內，dispatcher 與 kernel 兩端一起寫成 C 即為功能等價（見 [`code_pools.md`](code_pools.md) 與 [`rebuild_info/pitfalls.md`](../rebuild_info/pitfalls.md)）。

**兩個 AI 階段的進入點已定**：敵方階段 `fdps_battle_enemy_turn_phase`（`00012960`）與我方 NPC 階段 `fdps_battle_npc_turn_phase`（`00012b20`），配合 `fdps_battle_tick_status_effects`（`0001fa30`）與 `fdps_battle_run_turn_events`（`0002e0c0`）。章節重新初始化的實質內容是 `fdps_build_map_unit_array`（`00022be0`）：它依名冊陣列 `0x64108` 重建地圖單位記錄，超出名冊人數的槽位歸零並標成退場。

**仍未定的**：

- `fdps_render_view_frame`（`0002beb0`）每幀組出來的內容還沒定。它不收參數、有 41 個呼叫端，主體全部來自全域狀態；緩衝區 360×240 而只顯示 312×192，開頭 `0x21d8` byte 的用途未知。
- 離場旗標 `000643eb` 與 `Disk.no` 第三個 token 的緩衝區 `000643e8` 只差三個 byte。兩者是獨立全域還是同一個小緩衝的第 3 個 byte，沒有從 `main` 本身斷定；執行期無差別（旗標在讀檔後被明確歸零），但重建時緩衝區要宣告大小就必須先決定。
- 單位記錄的 byte +5 已由寫入端證實：bit 0 是退場、bit 7 是本回合已行動，而且退場是整個指派而非設位元。bit 1..6 在整個映像裡沒有任何寫入端——這個 byte 只被整個指派成 0 或 1、OR `0x80`／`0x1`、AND `0x7f`／`0xfe`／`0x1`——所以執行期恆為 0。bit 2 雖然被 `fdps_battle_system_menu`（`00014c9c`）與 `fdps_battle_player_phase_loop`（`0002bc89`）的 `AND AL,0x85` 測試，但沒有人設它，是死旗標；MP 回復的三處 `flags == 0` 測試實際上等於「未退場且本回合未行動」。bit 2 是沿用 FD2 的 `0x85` 遮罩留下的前作遺留，見 [`cut_content/code.md`](../cut_content/code.md)。byte +6（陣營）與 +0x26 的語意仍只從測試推得。
- 移動格子 cell 的 byte 0 只有 bit 6（`0x40`，有單位佔據）與 bit 7（`0x80`，進入即停）有意義。`fdps_field_load_chapter_resources` 以 malloc 配置整塊後只寫表頭就呼叫 `fdps_map_grid_reset`，後者以 `AND 0x3f` 保留低六位，所以低六位是堆積殘值；全程式沒有任何讀取端不先以 `0x40` 或 `0x80` 遮罩，殘值不影響行為。
- `fdps_set_flag_bit`（`000282b0`）與 `fdps_object_set_field34_low_nibble_range`（`00036b60`）的行為清楚但歸屬哪個子系統未定，要等它們操作的旗標語意定案。
