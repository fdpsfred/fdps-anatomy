# rle_asm — RLE 繪製組語的轉錄、比對與切換

票 22.3 把 `fdps_blit_dispatch` 與它底下的 14 支 RLE 繪製 routine 改回原版的手寫組語。本資料夾放比對工具、落地腳本、C 版／組語版切換工具與該票的 workflow。「執行效率相同」的定義、WASM 10.0a 的編碼事實由 [`rebuild_info/build_flags.md`](../../rebuild_info/build_flags.md) 擁有，檔案分組與改回 C 版的步驟由 [`rebuild_info/code_layout.md`](../../rebuild_info/code_layout.md) 擁有；本檔只講腳本與跑法。

## 腳本

| 檔案 | 用途 |
| --- | --- |
| `asm_match.py` | 比對工具。`listing ADDR` 印出原版 routine 給轉錄者看（每道指令的長度、分支落點的指令序號、重定位欄位已換成符號名）；`frag ASM...` 在 DOSBox-X 裡用 WASM 組譯獨立模組並逐支比對；`check` 比對 `src/` 的五個 `.asm`（預設讀 emittest 建置剛組出的物件檔，`--fresh` 當場組譯）；`selftest` 證明每一種不合格都抓得到 |
| `land.py` | 把 `workspace/rle_asm/frag/` 的片段依原版位址順序接成五個 `src/*.asm`。不做判斷：片段缺、沒通過、形狀不對就報告並一個檔都不寫。`--check` 只驗不寫 |
| `switch_impl.py` | 在「連組語」與「連 C 譯本」兩種狀態之間切換：翻轉所有 `RLE_C_REFERENCE` 標記區的 `#if 0`／`#if 1`，並把五個 `.asm` 停放到 `workspace/rle_asm/parked/` 或放回來。`status` 報目前狀態，半套狀態一律拒絕；`selftest` |
| `wf_state.py` | workflow 的檔案面。進度一律從檔案判斷：片段雜湊與判定檔是否相符（`stamp`／`verify-frag`）、落地是否完成（`verify-land`）、測試移植是否完成（`verify-tests`）、舊測試案例是否全部有人認領（`coverage`） |
| `rle_ticket223.js` | 票 22.3 的 Workflow：轉錄（一支一個 agent）→ 回掃 → 落地與 build gate → 測試移植（一支一個 agent，序列）→ 覆蓋檢查與最終 gate |

## 跑法

```
python tools/rle_asm/asm_match.py selftest
python tools/rle_asm/asm_match.py listing 000568db
python tools/rle_asm/asm_match.py check            # 讀 emittest 建置的物件檔
python tools/rle_asm/asm_match.py check --fresh    # 當場組譯 src/*.asm
python tools/rle_asm/asm_match.py check --objs workspace/game_build/out/obj   # 讀遊戲本體建置的物件檔
python tools/rle_asm/switch_impl.py status         # asm 或 c
```

`asm_match.py check` 已登記進 build gate（`rle_asm.check` 隨 `emittest` 目標、`rle_asm.check_game` 隨 `game` 目標，都需要不進版控的 `fdps_game_files/FDPS.LE`），兩支 selftest 也在 gate 裡。

Workflow：

```
Workflow({ scriptPath: "tools/rle_asm/rle_ticket223.js", args: { label: "t223-01" } })
```

可續跑：判定檔、片段雜湊、落地 commit 與測試 commit 就是進度，再呼叫一次會從還沒完成的地方接下去。

## 注意

- **原版的 routine 範圍取「本支入口到下一支入口」**，最後一支到 object 1 的結尾，不用 Ghidra 記錄的 function 大小。
- **重建側讀的是 WASM 的物件檔（OMF），不是連結後的執行檔。** 物件檔裡有每支 routine 的公開名稱、範圍、重定位與外部呼叫，不必連結就能單支比對。解析器只認 WASM 10.0a 實際產出的記錄型態，遇到不認得的一律拒絕；記錄的讀法沿用前作 FD2 的 `tools/program_analysis/crt_callee_match/extract_obj_bytes.py`。
- **組譯一律在 DOSBox-X 裡用 DOS 版 WASM**，經 `tools/fdps_build/build_min.py` 的機制；每次呼叫用自己的暫存目錄，所以多個轉錄 agent 可以同時跑。
- 中間產物（片段、判定檔、暫存組譯目錄、停放的 `.asm`、`check.json`）全部在 `workspace/rle_asm/`。
