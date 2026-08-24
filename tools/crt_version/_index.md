# tools/crt_version — 工具鏈版本判定

問「`FDPS.LE` 是用哪一個 Watcom 發行版建出來的」，並把答案收斂到單一版本。結論在 [`rebuild_info/build_flags.md`](../../rebuild_info/build_flags.md) 的工具鏈一節，本資料夾是取得那個結論的量測。

作法是把問題拆成每個 function 各答一次。映像檔裡每個 `crt` function 都是一次獨立的量測：它的 body 與哪些發行版的程式庫模組能逐 byte 對上，那些版本就與它相容；對不上的就被它排除。建置時用的那一版必須與**每一個** function 都相容，所以答案是所有相容集合的交集——這是一次計數，不是又一次判定。

比對的兩個對象分開處理，因為它們證明的事不同：

| 對象 | 來源 | 證明什麼 |
| --- | --- | --- |
| 執行期程式庫 | 11 個發行版（9.5–10.6a）的 `CLIB3S`、`MATH387S`、`EMU387`、`GRAPH`、`CSTRTX3S`，共 9,309 個 OMF module | 連結進來的程式庫是哪一版 |
| 編譯器 | 同一份語料用三個發行版的 DOS 版 `wcc386` 各編一次 | 兩個發行版的編譯器能不能從產出分辨；分辨得出的話，遊戲自己的程式碼形狀就能回答編譯器的版本 |

**比對只遮掉該模組自己的 FIXUPP 記錄宣告連結器會改寫的 byte。** 連結後的 body 與 `.obj` 之間必然在絕對位址與跨 object 的 rel32 位移上不同，那些位置要排除；除此之外每個 byte 都要相等。遮多了只會縮短可比長度、讓判定變弱，遮少了才會捏造出版本差異——所以兩支遮罩來源（指令自己的參照，加上一次「任何落在映像範圍內的 4-byte 小端值」掃描）刻意都往多遮的方向做。

## 檔案

| 檔案 | 用途 |
| --- | --- |
| `extract_versions.py` | 用 `wlib -q -x` 把每個安裝的 DOS 32-bit 堆疊慣例執行期拆成 `.obj`，過一次票 14 的 quirky record 修補，建出 `lib_index.json` |
| `omf_image.py` | 把一個 `.obj` 模組的 LEDATA 記錄組回連續的段影像，同時產生「哪些 byte 會被連結器改寫」的遮罩 |
| `DumpCrtBodies.java` | 從 Ghidra 匯出每個 `crt` function 的 body 位元組與可重定位位置 |
| `sweep_versions.py` | 每個 function 對每個發行版的每個模組找可能的對位，輸出一份證據包。**只輸出證據，不下判定** |
| `check_verdicts.py` | 判定的 gate：版本集合要涵蓋每個候選發行版恰好一次，證據欄位要指名具體的東西（模組、位移、byte 數、指令） |
| `pending.py` | 還沒有可用判定的位址；判定要對得上磁碟上現在這份證據包才算數 |
| `rescan_list.py` | 還需要第二次閱讀的位址：切分 10.0 家族的、信心不是 high 的、留有 open question 的 |
| `aggregate.py` | 把所有判定的相容集合取交集並計數，輸出 `aggregate.json` |
| `compiler_diff.py` | 用同一組旗標、同一份語料，比較不同發行版的 `wcc386` 產出的 `_TEXT`；`--headers-from` 可把每個發行版釘在同一份標頭檔上 |
| `CountCodeShapes.java` | 在 `FDPS.LE` 裡逐 pool 數 `compiler_diff.py` 找出來的那組編碼形狀 |
| `probes/` | 差分編譯用的探針原始碼 |
| `version_ticket16.js` | 票 16 的 workflow：一個 function 一個 agent，判定、跑 gate、回掃、收斂、寫知識庫 |

## 跑法

```bash
python tools/crt_version/extract_versions.py
```
```
run_ghidra_script DumpCrtBodies.java   args: <work>\crt_bodies.json pool_crt
```
```bash
python tools/crt_version/sweep_versions.py
```

逐 function 的判定由 workflow 從頭跑完，一個 function 一個 agent：

```
Workflow({ scriptPath: "tools/crt_version/version_ticket16.js", args: { roundSize: 12 } })
```

編譯器那一側是獨立的兩趟，第二趟把三個發行版釘在同一份標頭檔上，用來分辨「碼產生器不同」與「標頭檔的 inline 巨集不同」：

```bash
python tools/crt_version/compiler_diff.py
python tools/crt_version/compiler_diff.py --corpus tools/crt_version/probes --headers-from 10.0a
```
```
run_ghidra_script CountCodeShapes.java  args: <work>\code_shapes.json
```

`compiler_diff.py` 會在 DOSBox-X 裡跑 DOS 版的 `wcc386`，與原版的建置環境一致。

## 已知的坑

**每個 agent 只拿到一個證據包，所以它分不出「例外」與「常態」。** 一個判定看起來多合理都不代表它與其他 380 個一致，而切分 10.0 家族的那幾個判定本身就是決定整張票的少數派。回掃這一段因此不是可選的，而且它的工作清單要從判定檔自己推導（`rescan_list.py`），不能從某一次執行的記憶推導——跑到一半被中斷時，沒被讀第二次的判定仍然標記為未讀，下一次執行接得回來。

**gate 檢查的是證據不是 Ghidra。** 這張票不寫入程式，所以沒有孤立程式碼或 error bookmark 可查。這裡會出錯的是另一種形式：一份判定寫了相容集合卻沒寫比對了什麼，讀起來與健全的判定一模一樣，而事後無法重新檢查。所以 `check_verdicts.py` 擋的是空的證據欄位。

**掃描是以未遮蔽的長字串當錨點找對位，body 太短就問不出問題。** `MIN_ANCHOR` 之下的候選清單會膨脹到上千筆，那種 function 只能記成「沒有可比的 byte」。有一類是可以救的：同模組裡若有另一個 function 已經把模組的載入位址釘住，短 body 就能在已知的公開位移上直接對位比較，而不必靠錨點搜尋。`0004364b`（`__STKOVERFLOW`）與 `00042ca0`（`IF@TAN`，5 byte 卻切開了 10.0b）都是這樣救回來的。

這個位置回退法沒有做成掃描的通則，代價已經清點過：395 個裡剩 14 個問不出相容集合，每一個都只有 1–3 個可比 byte（多數是單一條 `JMP rel32` 或 `RET`），其中 4 個的判定階段已經以鄰居釘出了模組（`delay386`、`386inite`、`save8087`）並確認該模組跨版本未改。**把回退法通則化不會改變結論**：1–3 個 byte 在任何一版的程式庫裡都對得上，只能證實不能排除。

**`WATCOM_10.0a` 的 `clib3s.lib` 是殘缺的副本，兩份 10.0a 安裝要一起留著。** 它少一個 `stk386` 模組，五個堆疊檢查 stub 因此在那份安裝上比不到東西、被記成排除——那是檔案殘缺不是版本差異。哪一份副本是完整的是量出來的結果，不是預設值，所以 `extract_versions.py` 兩份都收、並排報告，由判定階段自己看出來。處置寫在 [`rebuild_info/pitfalls.md`](../../rebuild_info/pitfalls.md)。

**程式庫一律用 `wlib` 拆，不自己寫 `.lib` reader**，quirky record 的修補也沿用 [`tools/pool_triage/fid/`](../pool_triage/fid/_index.md) 的 `omf_patch_segdef.py`。`omf_image.py` 讀的是 `wlib` 寫出來的 `.obj`，從不自己打開 `.lib`。
