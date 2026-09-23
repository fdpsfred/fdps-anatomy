# game_build — 遊戲本體的建置、並排遊玩與偏差定位

把 `src/` 連結成取代 `FDPS.EXE` 的執行檔，讓原版與重建版能在同一套環境下並排遊玩，並把實機看到的當機位址換回 function。遊戲本體的連結契約、實機驗證的分工與定位方法由 [`rebuild_info/build_pipeline.md`](../../rebuild_info/build_pipeline.md) 與 [`rebuild_info/playtest.md`](../../rebuild_info/playtest.md) 擁有，本檔只講腳本與跑法。

建置本身不重寫：`build_game.py` 匯入 [`tools/fdps_build/`](../fdps_build/_index.md) 的 DOSBox-X 機制、[`tools/code_emit/`](../code_emit/_index.md) 的原始碼掃描與反編譯器命名檢查、[`tools/ail_link/`](../ail_link/_index.md) 的七條 alias 與 SB16 參數。

## 腳本

| 檔案 | 用途 |
| --- | --- |
| `build_game.py` | `build`（預設）編譯 `src/` 全部 `.c` 與 `.asm`、連結一次成 `FDE.EXE` 並寫 `FDE.MAP`；不編 `tests/`，`main` 保持原名。未解符號一律是錯誤，沒有 stub 那一段。`selftest` 不碰 DOSBox-X，驗連結指令檔與批次檔的產生、以及轉錄解析的每一種判定 |
| `play.py` | `stage` 把遊戲檔複製進該版本的目錄；`run` 開 DOSBox-X 給人玩；`boot` 無人值守地開機、依需要自動按鍵、在指定秒數截圖、掃保護模式故障後關閉；`save-copy` 把 `FDE.SAV` 從一版的目錄複製到另一版；`selftest` 驗 conf 產生 |
| `locate.py` | 把 DOS/4GW 當機傾印的 `Crash address (unrelocated)`（或 map 的 `0001:xxxxxxxx`、或符號名）換成重建版裡的 function 與它在哪個 unit，並以同名查出原版位址；`--original` 反過來從原版位址查。`selftest` 驗位址解析與兩邊的查找 |

## 跑法

```
python tools/game_build/build_game.py                 # 建出 FDE.EXE
python tools/build_gate/gate.py                        # 修正之後：完整的 gate（含單元測試）
python tools/game_build/play.py run rebuilt            # 開重建版來玩
python tools/game_build/play.py run original           # 開原版來玩
python tools/game_build/play.py run rebuilt --no-sound # 拔掉音效卡的那一輪
python tools/game_build/play.py save-copy original rebuilt   # 同一份存檔換版本
python tools/game_build/play.py boot rebuilt --keys "enter enter" --key-wait 30 --key-pace 4 --shots 32,40
python tools/game_build/locate.py 1:0000002A           # 當機位址 -> function
python tools/game_build/locate.py --original 2a4b1     # 原版位址 -> 重建版的同一支
```

兩版的目錄在 `workspace/game_build/play/ORIGINAL/` 與 `REBUILT/`。兩版要一致的條件（哪些檔、哪個延伸器、模擬器設定、為什麼不帶調色盤快取）由 [`rebuild_info/playtest.md`](../../rebuild_info/playtest.md) 擁有，`play.py` 照它做。

## 注意

- **目錄在重跑之間保留**，玩出來的存檔留在原地；`stage` 只在目錄裡沒有 `FDE.SAV` 或帶 `--fresh-save` 時才放入出貨的那份。每次 `stage` 都會刪掉上一輪留下的調色盤快取與結束標記，大檔只在大小或修改時間不同時才重新複製。
- **遊戲一回到 DOS，guest 就寫出 `GAMEEXIT.TXT`**，接著 `pause` 讓 DOSBox-X 停在那個畫面上。DOS/4GW 的當機傾印印在 guest 的文字畫面，不進任何 log；沒有這個標記，從 host 看當機與正常執行沒有差別。`boot` 看到標記就判失敗，最後一張截圖就是遊戲退出後的畫面。
- **`boot` 會在桌面開一個視窗**（不能加 `-silent`，見 `playtest.md`）。
- **截圖先問視窗有沒有回應。** `PrintWindow` 要視窗自己重繪，而 DOSBox-X 的視窗偶爾會被 Windows 判成「沒有回應」，那時 `PrintWindow` 會無限等待。所以先用 `IsHungAppWindow` 擋，再在 5 秒期限的執行緒裡截圖；截不到就記在 `screenshot_problems`，`boot` 判失敗。
- **`boot` 的範圍只到開機並進入遊戲**，理由見 `playtest.md` 的分工一節。
