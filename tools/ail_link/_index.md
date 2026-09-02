# ail_link — AIL 靜態庫的連結與實跑驗證

把前作 FD2 抽出的 `ailv3.lib` 用 FDPS 的旗標連進一支客戶端程式，在 DOSBox-X 裡實際初始化音效並播放。這是 [ADR-0004](../../docs/adr/0004-reuse-fd2-ail-library.md) 從「比對到的證據」變成「連得起來也跑得動」的那一步。結論在 [`rebuild_info/ail_link.md`](../../rebuild_info/ail_link.md)。

DOSBox-X 的驅動機制（前置檢查、conf／批次檔產生、三訊號結束偵測、故障掃描）匯入 [`tools/fdps_build/`](../fdps_build/_index.md) 的 `build_min.py`，不重寫一份。

## 腳本

| 檔案 | 用途 |
| --- | --- |
| `link_ail.py` | 四個子命令。`build` 編譯 `ailsmoke.c` 與 `src/dpmi.c`、組譯 `src/ailflags.asm`，連上 `ailv3.lib` 與三個 CRT 程式庫；`run` 把產出連同 `DIG.INI`、`SB16.DIG` 放進執行目錄，掛上 Sound Blaster 後執行並逐欄比對它寫出的 `RESULT.TXT`；`all`（預設）依序跑兩者；`selftest` 拿掉一條 alias 重連，要求連結器真的報出解不掉的符號 |
| `ailsmoke/ailsmoke.c` | 客戶端程式。呼叫遊戲用到的 AIL 進入點，播一段方波、註冊一個計時器 callback，把每一項觀察寫成 `RESULT.TXT` 的 key=value |

`ALIASES` 是七條 wlink `alias` 的正本——庫的 EXTDEF 用前作的符號名，FDPS 這邊用自己的名字，接點只在這裡。

## 跑法

```
python tools/ail_link/link_ail.py            # 建置 + 執行，全過才回 0
python tools/ail_link/link_ail.py build      # 只編譯連結
python tools/ail_link/link_ail.py run        # 只執行既有的產出
python tools/ail_link/link_ail.py selftest   # 驗證未解析符號的檢查本身會失敗
```

路徑覆寫與 `build_min.py` 相同（`FDPS_WATCOM`、`FDPS_DISC1`）。中間產物與產出落在 `workspace/ail_link/`。

## 注意

- **`selftest` 要一起跑。** 本工具的主要結論是「未解析符號 0 個」，而一個永遠不會失敗的檢查在壞掉的連結上也會這樣回報。它刻意拿掉一條 alias，要求 `parse_undefined` 真的抓到那個符號。
- **執行階段不能加 `-silent`。** DOSBox-X 在 silent 模式下連 Sound Blaster 的模擬一起關掉，症狀是 `AIL_install_DIG_INI` 回 NULL 而錯誤碼是 0。建置階段照舊用 `-silent`。
- 執行階段需要 `fdps_game_files/` 的 `DIG.INI` 與 `SB16.DIG`，那個資料夾不進版控。
