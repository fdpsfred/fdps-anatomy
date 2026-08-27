# 資料結構與全域資料

**驗證對象**：`FDPS.LE` 中所有被程式碼當成資料取用的位址——可寫的 `.object2`（`0x60000`–`0x6c3bf`）、`.object3`（`0x70000`–`0x70053`），以及夾在 `.object1` 程式碼之間、不屬於任何 function body 的唯讀資料。全域符號的身分、型別與所屬 pool，以及執行期記錄的欄位佈局，以此檔為唯一正典。

位址空間的分段與權限由 [`memory_layout.md`](memory_layout.md) 擁有；function 的 pool 歸屬由 [`code_pools.md`](code_pools.md) 擁有；遊戲數值本身由 [`assets/`](../assets/_index.md) 擁有，本檔只講「那些數值排成什麼形狀」。

## 判定的單位是 anchor，不是變數

參照落在哪裡是事實，變數從哪裡開始是判斷。程式碼一天到晚參照陣列與結構的中間，所以每個「有指令當成資料讀寫的位址」先當成一個 **anchor** 送去判定，第一個問題是「這是不是一個變數」。1,040 個 anchor 判完之後：

| 判定 | 數量 | 意思 |
| --- | --- | --- |
| `interior` | 373 | 不是獨立符號，是某個從更低位址開始的物件的內部。名字由擁有者的型別給 |
| `array` | 315 | 定長記錄的表，或字元／數值陣列 |
| `variable` | 267 | 純量 |
| `pointer` | 81 | 指標。**把它判成整數是這裡最常見的錯**，而把它標成指標正是反編譯變好讀的主因 |
| `padding` | 4 | 對齊填充 |

pool 分布是 `binary_artifact` 395、`fdps` 292、`crt` 196、`ail` 157。判準是**誰取用它**：只有某一個 pool 的程式碼碰得到的全域就屬於那個 pool，跨 pool 時判斷誰是擁有者、誰只是訪客。

346 個字串字面值不在清單上——重建後它們是敘述句裡的字面值而不是具名全域，理由見 [`rebuild_info/naming.md`](../rebuild_info/naming.md)。

## 遊戲的執行期記錄

所有遊戲資料表都**不在執行檔裡**，執行時從資源檔讀進堆積，執行檔裡放的是指向它們的指標。指標全部集中在 `0x63fd0`–`0x63ff0` 這一段：

| 型別 | 大小 | 指標所在 | 內容 |
| --- | --- | --- | --- |
| `fdps_unit_record` | 0x50 | `0x69cd8` `data_fdps_map_unit_array_ptr`、`0x64108` `data_fdps_roster_array_ptr` | 戰場單位的執行期狀態，以 unit index 索引 |
| `fdps_item_effect` | 23 | `0x63fe0` `data_fdps_item_effect_table_ptr` | 一件道具的全部效果欄位 |
| `fdps_spell_effect` | 7 | `0x63ff0` `data_fdps_battle_spell_effect_table_ptr` | 一個法術的效果 |
| `fdps_class_record` | 10 | `0x63fd0` `data_fdps_class_table_ptr` | 一個職業 |
| `fdps_class_equip_record` | 6 | `0x63fe4` `data_fdps_class_equip_table_ptr` | 職業可裝備的道具位元組 |
| `fdps_character_base_record` | 24 | `0x63fd8` `data_fdps_battle_character_base_table_ptr` | 角色出場屬性 |
| `fdps_character_growth` | 11 | `0x63fec` `data_fdps_battle_character_growth_table_ptr` | 角色成長 |
| `fdps_promotion_record` | 12 | `0x63fdc` `data_fdps_promotion_table_ptr` | 轉職 |
| `fdps_spell_learning_record` | 12 | `0x63fe8` `data_fdps_spell_learning_table_ptr` | 法術習得 |
| `fdps_enemy_data` | 10 | `0x63fd4` `data_fdps_battle_enemy_data_table_ptr` | 敵人 |
| `fdps_char_spawn_record` | 26 | 章節資料內 | 出場配置 |
| `fdps_map_spawn_pos_record` | 6 | 章節資料內 | 出場座標 |
| `fdps_tile_attr_entry` | 4 | — | 地形屬性 |
| `fdps_move_grid_cell` | 2 | — | 移動範圍格 |
| `fdps_map_cell_code_layer` | 17 | `0x60148` `data_fdps_map_cell_event_code_layer_ptr` | 每格的事件碼平面 |
| `fdps_save_slot` | 2600 | — | 一個存檔槽 |
| `fdps_cel_header` | 15 | `0x643ac` 起數個 sprite sheet 指標 | CEL 檔頭 |
| `fdps_cel_cache_slot` | 48 | — | sprite 快取槽 |
| `fdps_palette_entry` | 3 | `0x643bc`、`0x643e4` | VGA 調色盤一格 |
| `fdps_vfs_image_header` | 11 | `0x643a0`、`0x643a8` | VFS 容器檔頭 |
| `fdps_cd_request_header` | 26 | `0x69de8` | MSCDEX 請求標頭 |
| `fdps_cd_q_channel_block` | 11 | `0x69e56` | CD Q channel |
| `fdps_dpmi_real_mode_call` | 50 | `0x69e22` | DPMI real-mode call structure |

**沒有 per-chapter 記錄。** 章節資料不是執行檔裡的一張表；沒有任何程式碼以固定 stride 索引章節結構。章節的分派走的是 [`memory_layout.md`](memory_layout.md) 記的那四張函式指標表，資料則整章從資源檔讀。

## 與前作 FD2 的對應

同一個引擎的前一代把這些記錄解過一遍（`fd2-anatomy/src/include/types.h`），所以每個佈局都先拿 FD2 的答案當起點，再逐欄回到 FDPS 自己的程式碼確認。**尺寸相同不等於佈局相同**，而這條不是理論上的顧慮：

- `fdps_unit_record` 與 FD2 的 `runtime_char` 都是 0x50 byte，**逐欄確認相符**。
- `fdps_item_effect` 與 FD2 的 `item_effect` 都是 23 byte，**佈局不同**。照 FD2 的欄位順序寫會編得出來、跑得動、數值全錯。
- `fdps_enemy_data`、`fdps_character_growth`、`fdps_spell_effect`、`fdps_char_spawn_record` 與 FD2 對應版本逐欄相符。
- `fdps_class_record` 沒有對應：FD2 把同樣的三個量放在三張分開的表，FDPS 併成一筆記錄。

## 程式庫的記錄

CRT 與 AIL 的結構用**程式庫原名，不加前綴**（`tm`、`FILE`、`REGS`、`SREGS`、`SAMPLE`、`DIG_DRIVER`…），判定不出程式庫名稱、只存在於單一 object 內的用 `L$N_`。理由見 [`rebuild_info/naming.md`](../rebuild_info/naming.md)。

值得記下來的幾個：

| 型別 | 大小 | 所在 | 備註 |
| --- | --- | --- | --- |
| `FILE` | 26 | `0x61150` `__iob` | Watcom 的 `FILE`，26 byte 不是 32 |
| `__stream_link` | 8 | `0x6a2fc` `__ClosedStreams`、`0x6a300` `__OpenStreams` | stdio 串流鏈結，**自我參照**（`next` 指向自己這個型別） |
| `tm` | 36 | `0x6145c` `__start_dst`、`0x61480` `__end_dst` | Watcom 的 `tm` 是 36 byte |
| `miniheapblkp` | 44 | `0x60388` `__nheapbeg` | 近端堆積的區塊標頭 |
| `rt_init` | 6 | `0x638f0`、`0x63920` | XI／YI 啟動與收尾鏈的節點 |
| `long_double_80` | 10 | 多處浮點常數 | 80-bit extended real，與 FD2 的同名型別相同 |
| `L$N_emu387_state` | 122 | `0x613c8` | 80x87 模擬器的完整狀態，**必須當成一塊連續記錄** |
| `SAMPLE` | 2196 | `0x69d30` | Miles AIL 的取樣記錄，FD2 當成不透明 handle 沒有解開 |
| `SEQUENCE` | 1748 | `0x6a0d8` | 同上 |

`SAMPLE`、`SEQUENCE`、`DIG_DRIVER`、`AIL_DRIVER`、`VDI_CALL`、`IO_PARMS` 是本專案比前作多走的一段：FD2 連結 `ailv3.lib` 並把所有 driver handle 當成 `void *`，沒有打開過這些記錄。

## 資料段與未初始化段的分界

`.object2` 裡最後一個非零的初始化 byte 落在 `0x6392a`，在 `_edata`（`0x63930`）之下。這個量測每一輪落地都重跑一次，與 [`memory_layout.md`](memory_layout.md) 由 CRT 啟動碼立即值得出的邊界一致。

## 這份判定怎麼查證

每個符號的名稱、型別與說明隨 Ghidra 快照進版控：[`ghidra_snapshot/data.txt`](../ghidra_snapshot/data.txt) 是資料項與型別，[`labels.txt`](../ghidra_snapshot/labels.txt) 是符號，[`data_types.txt`](../ghidra_snapshot/data_types.txt) 是 struct 佈局，[`comments.txt`](../ghidra_snapshot/comments.txt) 是每個全域的說明與重建注意事項。判定與稽核工具見 [`tools/global_data/`](../tools/global_data/_index.md)。
