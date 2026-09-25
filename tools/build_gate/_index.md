# build_gate — 自我回歸閘

重建一次、與記錄的基準值比對、跑測試套件，回一個結構化的判定。判定規則、六種等價判定的意義、基準值什麼時候該推進，全部由 [`rebuild_info/build_gate.md`](../../rebuild_info/build_gate.md) 擁有；本檔只講腳本與跑法。

建置本身不在這裡實作——`gate.py` 匯入 [`tools/fdps_build/`](../fdps_build/_index.md) 與 [`tools/ail_link/`](../ail_link/_index.md) 的建置函式，閘門與建置流程共用同一份 DOSBox-X 驅動機制。

## 腳本

| 檔案 | 用途 |
| --- | --- |
| `gate.py` | 四個子命令。`check`（預設）建置每個目標、比對基準值、跑測試套件；`update` 推進某個目標的基準值，需要 `--reason`；`show` 印出目前的基準值與推進鏈；`selftest` 不碰 DOSBox-X，用自己合成的 LE 映像驗證閘門的每一項判定 |
| `lefixup.py` | LE 重定位解析（每筆 record 的 site 與內部目標）與重定位感知的映像指紋；給了空隙清單時指紋另帶空隙位置與抹掉空隙後的殘差，`compare` 據此判 `pad` |
| `lepad.py` | 從一次建置的映像、map、OMF 目的檔與暫存的原始碼，算出 `wcc386` 沒清零、而且證明得出來的對齊空隙（`CONST` 的字面值空隙、`_DATA`／`CONST2` 的符號空隙）。規則由 [`rebuild_info/build_gate.md`](../../rebuild_info/build_gate.md) 擁有。`selftest` 用合成的建置驗證空隙的位置與每一種該判 `different` 的改動，已排進測試套件 `build_gate.pad_selftest` |
| `pad_diff.py` | 診斷工具：`pad_diff.py [--target game\|ailsmoke] 舊根目錄 新根目錄`，兩個根目錄各自建過該目標（通常是乾淨的 worktree 與主工作目錄）。用與閘門相同的規則把每個不同的 byte 分成重定位、空隙、無法解釋三類，無法解釋的列出 object 位移與前後的 map 符號。閘門報 `different` 時用它找出是哪個 byte |
| `data/baselines.json` | 基準值，一個建置目標一筆，含推進鏈。**進版控**——放到 `workspace/` 下會跟中間產物一起消失 |

`TARGETS` 是建置目標的正本。重建推進時新增一個目標再跑一次 `update`，就是全部的擴充工作。`TEST_SUITES` 是測試套件的正本，每筆帶 `needs` 與 `target`：機器缺件、或 `--target` 沒選到它對應的建置目標時報 skip，不會靜默略過，也不會跑到範圍外的東西。

## 跑法

```
python tools/build_gate/gate.py                     # 全部目標，建置 + 比對 + 測試
python tools/build_gate/gate.py check --target smoke --skip-tests
python tools/build_gate/gate.py check --json        # 結構化結果印到 stdout
python tools/build_gate/gate.py check --with-audio  # 連音效實跑套件一起
python tools/build_gate/gate.py show
python tools/build_gate/gate.py update --target smoke --reason "..."
python tools/build_gate/gate.py selftest
```

路徑覆寫與 `build_min.py` 相同（`FDPS_WATCOM`、`FDPS_DISC1`）。結果落在 `workspace/build_gate/result.json`。

要在建置腳本裡前景呼叫就 `import gate` 用 `gate.check(...)`，回傳的 dict 與 `--json` 印的是同一份。離開碼 0 只在整個閘門通過時給。

## 注意

- **`selftest` 要一起跑**，而且它已經被排進 `check` 的測試套件裡。閘門的每一項判定都必須被證明會失敗過；永遠不會 FAIL 的閘門在壞掉的建置上也會說 PASS。
- **`game` 目標是遊戲本體 `FDE.EXE`**（[`tools/game_build/`](../game_build/_index.md)），有映像基準值；另帶 `data_emit.check_game` 與 `rle_asm.check_game`，同樣兩項比對換成對遊戲映像跑。
- **`game` 與 `ailsmoke` 的比對帶對齊空隙**：建置後讀該次的 map（`FDE.MAP`、`AILSMOK.MAP`）、目的檔目錄與暫存的原始碼目錄算空隙；map 讀不到或格式不對時，這個目標的 `pad` 檢查直接失敗，不會安靜地退回沒有空隙的比對。結果列裡的 `pad` 欄記空隙數、byte 數，以及大小取不到的符號與證明不了的 `CONST` 項目。
- **`emittest` 目標帶資料比對套件 `data_emit.check`**：已落地的全域逐 byte 對原版 `FDPS.LE`（[`tools/data_emit/`](../data_emit/_index.md)）。它需要不進版控的 `fdps_game_files/FDPS.LE`，沒有就報跳過。
- **`--with-audio` 會開一個視窗**，而且需要不進版控的 `fdps_game_files/`；預設不跑，跳過的套件會列在結果裡。
- 閘門建置時照樣掛光碟。編譯讀不到 `E:`，但閘門必須用**與正常建置相同的方式**建置，否則它閘的不是真正會出貨的那個流程；掛載定義由 `build_pipeline.md` 決定，兩個階段共用。
- 光碟的有無用「`.cue` 存在**且**它指名的 `.bin` 存在」判定，與執行階段的前置檢查同一條件。只檢查 `.cue` 會讓懸空的 cue 通過，接著讓 `fdps_build.run` 失敗，把「機器少了光碟映像」報成回歸。
