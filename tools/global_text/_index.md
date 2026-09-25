# global_text — 不屬於任何一章的文字

產生 [`assets/text/global_text.md`](../../assets/text/global_text.md)（`FDETXT00.TXT` 全文、語意分區與每區的讀取端）與 [`assets/text/scene_text.md`](../../assets/text/scene_text.md)（`FDETXT31`–`FDETXT65` 額外場景裡會顯示的文字與顯示它們的腳本）。永遠不會顯示的條目不寫進頁面；要清單的工作（票 25.14）直接 import `load_blocks`、`load_scripts`、`scene_refs` 與 `classify_scene_block` 取得。兩頁都是產生物，要改內容就改本工具再重跑，不直接編輯。

前作 FD2 的對應成果是 `fd2-anatomy/assets/text/global_text.md` 與 `endgame_text.md`，本工具沿用它的頁面形式（名稱表以「欄位值 + 常數」定位、系統訊息分組列出讀取端）；FDPS 的區塊格式、分區與讀取端全部取自 FDPS 自己的來源。

## `global_text.py`

```
python tools/global_text/global_text.py build  [--game 遊戲目錄]
python tools/global_text/global_text.py verify [--game 遊戲目錄]
```

遊戲目錄預設是 `fdps_game_files/`。

- `build`：跑完下列閘門後寫出兩頁。
- `verify`：同樣的閘門，再把兩頁與重新產生的內容逐字比對，不一致就回非零。

資料來源，全部不手打：

- 原文：直接 import [`text_decode`](../text_decode/_index.md)，容器讀取借 [`cutscene_script`](../cutscene_script/_index.md) 的 `read_container`。
- `FDETXT00` 的讀取端：先以 [`code_emit`](../code_emit/_index.md) 的 `strip_c` 清掉註解與字面值，再掃描 `src/` 裡每個 `fdps_draw_text(data_fdps_all_game_text_ptr, …)` 呼叫，以及每個寫入兩個代入槽（`data_fdps_dialog_last_action_text_id_param`、`data_fdps_dialog_subst_text_id_2`）的地方，條目運算式經同檔的 `#define` 解開。解得開的形狀只有三種：全是巨集與常數、一個執行期值加一個巨集、以及只被賦予巨集值的區域變數；執行期值若是有常數初值的區域表的一格，可到的條目就是基底加表中每個值。其他形狀一律報錯，不猜。
- 章節區塊前 9 條的讀取端：同樣掃描，對象是 `data_fdps_current_chapter_text_ptr` 與存讀檔畫面自己載入的 `chapter_text`，只收寫死的條目。條目是區域變數、而繪製包在 `if (變數 != 某值) {` 或 `if (變數) {` 裡時，被擋掉的值不算讀取端（`guarded_out_values`；第 16 章流浪工匠的回應編號初值 0 就是這樣剔掉的）；沒有讀取端的條目在表裡寫「沒有讀取端」。條目來自資料的呼叫（腳本的 `DRAW_TEXT`、死亡腳本、單位索引加偏移、片尾字幕）會不會落在前 9 條是資料的問題，由各章頁回答，這裡刻意略過。
- 額外場景的條目由誰顯示：直接 import `cutscene_script` 的 `decode_all`，取每支腳本的 `trace.text_refs`（`DRAW_TEXT` 與 `ASK_THREE_WAY`）與 `SWITCH_MAP` 目標。

手寫的只有 `FDETXT00` 的分區（`REGIONS`）、系統訊息的分組（`MESSAGE_GROUPS`）、沒有讀取端的條目（`NO_READER`）與頁面的說明文字。閘門檢查手寫部分與掃描結果一致：

- 分區首尾相接且剛好蓋滿整個區塊，訊息分組剛好蓋滿訊息區
- 每個「值 + 常數」的讀取端，常數都是某一個分區的起點
- 每個寫死條目的讀取端都落在訊息區，訊息區每一條有內容的條目都有讀取端
- 空白分區裡全是空字串；有內容又沒有讀取端的只有 `NO_READER` 列出的，而 `NO_READER` 的條目確實沒有讀取端
- 66 支腳本的追蹤沒有任何問題，腳本顯示的額外場景條目沒有空字串，也不含代入碼或數字碼

## 測試

```
python -m unittest tools/global_text/test_global_text.py
```

兩個介面各有手寫原始碼或手算期望值的測試：`scan_readers()`（巨集常數、十六進位與八進位字面值、欄位加基底、區域變數、區域表、代入槽、註解與代入分派不算讀取端、解不開或區域變數被累加就報錯）與 `classify_scene_block()`。另一組在 `fdps_game_files/` 存在時對出貨資料與目前的 `src/` 跑全部閘門，並釘住幾個已知名稱的位置與「只有地圖 30、31、33、49 沒有腳本切過去」。
