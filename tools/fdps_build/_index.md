# fdps_build — 在 DOSBox-X 內全自動建置

把原始碼在 DOSBox-X 裡以 Watcom 10.0a 的 DOS 版工具編譯、連結成 DOS/4G 執行檔並實際跑起來，全程不需人工介入。流程的結論記在 [`rebuild_info/build_pipeline.md`](../../rebuild_info/build_pipeline.md)，旗標組的判定依據在 [`rebuild_info/build_flags.md`](../../rebuild_info/build_flags.md)。

目前的建置對象是最小驗證程式；後續階段換掉編譯清單即可，其餘機制（前置檢查、conf／批次檔產生、三訊號結束偵測、故障掃描）照用。**這套機制的正本就在 `build_min.py`**，別的工作直接 import 它而不是抄一份——[`tools/ail_link/`](../ail_link/_index.md) 是第一個這樣做的。`launch()` 的 `silent` 參數是為那邊開的：DOSBox-X 的 `-silent` 會連 Sound Blaster 的模擬一起關掉。

## 腳本

| 檔案 | 用途 |
| --- | --- |
| `build_min.py` | 四個子命令。`build` 編譯連結 `smoke/smoke.c`；`run` 把產出放進執行目錄、掛上光碟映像後在 DOSBox-X 裡執行並驗證它寫出的結果檔；`all`（預設）依序跑兩者；`selftest` 不碰 DOSBox-X，直接驗證結束偵測的四種判定與前置檢查的三種報錯 |
| `smoke/smoke.c` | 最小驗證程式。確認的項目與觀察到的結果列在 [`build_pipeline.md`](../../rebuild_info/build_pipeline.md) |

## 跑法

```
python tools/fdps_build/build_min.py            # 建置 + 執行，全過才回 0
python tools/fdps_build/build_min.py build      # 只編譯連結
python tools/fdps_build/build_min.py run        # 只執行既有的產出
python tools/fdps_build/build_min.py selftest   # 驗證偵測與檢查機制本身
```

Watcom 安裝與光碟映像的路徑可用 `FDPS_WATCOM` 與 `FDPS_DISC1` 覆寫，預設分別是完整的 `WATCOM_10.0a_infobase` 與 `FDPS_DISC_1.cue`。中間產物與產出全部落在 `workspace/fdps_build/`。

## 注意

- **`selftest` 要一起跑。** 結束偵測若退化成「等到逾時才收工」，`build` 與 `run` 照樣會通過——掛住與跑完在成功路徑上長得一樣，只有負面案例分得出來。它的殘缺安裝檢查用的是本機真正殘缺的那份 `WATCOM_10.0a`，不是捏造出來的情境。
- 執行階段的判準是程式自己寫出的結果檔逐欄比對，不是 DOSBox-X 的離開碼——它一律回 0。
