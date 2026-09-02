# AIL 靜態庫的連結契約

怎麼把前作 FD2 從 `FD2.LE` 抽出的 Miles AIL 靜態庫連進 FDPS 的重建版。決定沿用而不重抽的理由在 [ADR-0004](../docs/adr/0004-reuse-fd2-ail-library.md)，pool 判定與逐 function 的比對結果在 [`program_info/code_pools.md`](../program_info/code_pools.md)；本檔擁有的是**連結這一側的契約**：庫要什麼、遊戲要給什麼、兩邊名字對不上時在哪裡接。

程式庫本體與標頭放在 `libs/ailv3/`，是前作產物的逐 byte 副本。驗證腳本在 [`tools/ail_link/`](../tools/ail_link/_index.md)。

## 庫向外要的東西只有兩類

`ailv3.lib` 由 `ail_code` 與 `ail_data` 兩個模組組成，模組之間的參照在庫內解決。它的 EXTDEF 記錄指向庫外的只有兩類符號：

| 類別 | 內容 | 由誰提供 |
| --- | --- | --- |
| Watcom CRT | `open`／`close`／`read`／`write`／`filelength`、`fopen`／`fclose`／`fgets`／`fprintf`／`sprintf`、`malloc`／`free`、`getenv`／`time`／`localtime`／`asctime`／`isatty`／`setbuf`、`strcpy`／`strlen`／`strncpy`／`strncmp`／`strnicmp`／`toupper`／`memset`／`memmove` | `clib3s.lib`，照 [`build_flags.md`](build_flags.md) 的連結指令列入即可 |
| 遊戲自己寫的 | 六支 DPMI 服務常式，加上一支存 EFLAGS 並關中斷的常式 | 重建版的 `src/`，見下節 |

第二類是唯一需要動手的部分。方向是**庫指向遊戲**：廠商 object 以 EXTDEF 參照這些符號，所以不管換哪一份 AIL 都不會附帶它們。

## 遊戲要給的七個符號

| 庫裡的 EXTDEF 名稱 | FDPS 的符號 | 位址 | 出處 |
| --- | --- | --- | --- |
| `fd2_dpmi_alloc_dos_memory` | `fdps_dpmi_alloc_dos_memory` | `0003ca49` | `src/dpmi.c` |
| `fd2_dpmi_free_dos_memory` | `fdps_dpmi_free_dos_memory` | `0003cad2` | `src/dpmi.c` |
| `fd2_dpmi_lock_region` | `fdps_dpmi_lock_region` | `0003cb01` | `src/dpmi.c` |
| `fd2_dpmi_unlock_region` | `fdps_dpmi_unlock_region` | `0003cb6e` | `src/dpmi.c` |
| `fd2_dpmi_lock_size` | `fdps_dpmi_lock_size` | `0003cb93` | `src/dpmi.c` |
| `fd2_dpmi_unlock_size` | `fdps_dpmi_unlock_size` | `0003cbaa` | `src/dpmi.c` |
| `crt_equivalent_get_eflags_thunk` | `AIL_internal_isr_eflags_save_cli` | `00044dc0` | `src/ailflags.asm` |

**左右兩欄的名字不同，靠 wlink 的 `alias` 指令接。** 庫是替 FD2 打包的，EXTDEF 裡寫的是 FD2 對這些常式的拼法；FDPS 這邊的符號依 [`naming.md`](naming.md) 的鐵則必須與 Ghidra 逐字相同。把任一側改名都會破壞那條鐵則，所以接點放在 `.lnk`：

```
alias fd2_dpmi_lock_size=fdps_dpmi_lock_size
```

七條 alias 的完整清單由 `tools/ail_link/link_ail.py` 的 `ALIASES` 擁有，是唯一的正本。

最後一條的兩邊差得更遠。前作把那 4 個 byte（`PUSHFD`／`POP EAX`／`CLI`／`RET`）判成 CRT 的等價實作、收在 `fd2common.lib`，而且庫是透過一個 5-byte thunk 呼叫它的；FDPS 判定它是 AIL 自己的（見 [`pitfalls.md`](pitfalls.md)）。thunk 只是廠商 `.LIB` 把同一支 helper 收兩份時的連結產物，沒有對應的原始碼，所以 alias 直接落在本體上，不另外造一個 thunk。

這支常式寫不成 C——`PUSHFD` 與 `CLI` 沒有 C 的寫法，而且它不能碰任何其他東西——所以它是 `.asm`，用 `wasm` 組譯。`wasm` 在 10.0a 的落點見 [`build_pipeline.md`](build_pipeline.md)。

## 遊戲用到的 AIL 介面全部在庫裡

`FDPS.LE` 的遊戲程式碼一共呼叫 18 個 AIL 進入點：

```
AIL_startup                 AIL_shutdown                AIL_install_DIG_INI
AIL_install_MDI_INI         AIL_register_timer          AIL_set_timer_frequency
AIL_start_timer             AIL_allocate_sample_handle  AIL_allocate_sequence_handle
AIL_init_sample             AIL_set_sample_address      AIL_set_sample_type
AIL_set_sample_playback_rate AIL_set_sample_volume      AIL_set_sample_loop_count
AIL_start_sample            AIL_stop_sample             AIL_sample_status
```

18 個全部是 `ailv3.lib` 發佈的符號，一個不缺。`tools/ail_link/ailsmoke/ailsmoke.c` 呼叫其中 17 個；剩下的 `AIL_allocate_sequence_handle` 需要一個 MDI driver handle 才呼叫得到，而 FDPS 的安裝拿不到（見下節），所以它只驗證到「符號存在、連結解得掉」。

## 執行期要有的檔案

| 檔案 | 用途 |
| --- | --- |
| `DIG.INI` | `AIL_install_DIG_INI` 讀的設定檔，指名驅動程式與 IO 參數。原版的 `IRQ`／`DMA` 都是 `-1`，代表由 `BLASTER` 環境變數自動偵測 |
| `SB16.DIG`（或 `DIG.INI` 指名的其他 `.DIG`） | 驅動程式映像，由 AIL 載入並呼叫 |

`AILDRVR.LST` 不是執行期相依。它自稱是「Sound driver installation message file」，內容是給安裝程式挑驅動程式用的選單與偵測規則；把它從執行目錄拿掉，`AIL_install_DIG_INI` 與播放都不受影響。

**FDPS 沒有 `MDI.INI`，也沒有任何 `.MDI` 驅動程式。** 遊戲照樣呼叫 `AIL_install_MDI_INI`，在原版的安裝上它就會回 NULL——音樂走的是 CD 音源（[`program_info/cd_audio.md`](../program_info/cd_audio.md)），AIL 只負責音效。重建版要保留這個呼叫與它失敗的路徑，不要因為「安裝 MDI 失敗」就中止。

## 已經驗證到什麼程度

`python tools/ail_link/link_ail.py` 在 DOSBox-X 裡跑完整條路，逐項確認：

| 項目 | 結果 |
| --- | --- |
| 連結 | `ailv3.lib` + 遊戲側七個符號 + 三個 CRT 程式庫，未解析符號 0 個，產出 DOS/4G LE 執行檔 |
| `AIL_startup` | 成功 |
| `AIL_install_DIG_INI` | 以原版的 `DIG.INI` + `SB16.DIG` 成功取得 driver handle |
| 混音與 DMA | 11,025 Hz 的 8-bit mono 方波播完，`AIL_sample_status` 走過 `PLAYING` 再到 `DONE` |
| 計時器 ISR | `AIL_register_timer` 註冊的 callback 實際被觸發 |
| `AIL_install_MDI_INI` | 回 NULL，與原版安裝一致 |
| 關閉 | `AIL_shutdown` 正常返回 |

計時器那一項是刻意加的：ISR 路徑正是遊戲側 DPMI 鎖頁常式存在的理由，API 裡沒有別的東西會走到它。

`link_ail.py selftest` 拿掉一條 alias 重連，要求連結器真的報出那個解不掉的符號——「0 個未解析符號」是本頁的主要結論，而永遠不會失敗的檢查在壞掉的連結上也會這樣回報。
