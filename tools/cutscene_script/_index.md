# cutscene_script — 過場腳本解碼

把 `ICONANI.VFS` 的 66 支過場腳本（`ICONnn`、`WINnn`、`GOODEND`、`WINGA26`、`ICON7-x`、`WIN17-1`）解成逐步的指令列表，並標出每一步當下的地圖、文字區塊與單位身分。格式結論記在 [`resource_info/cutscene_script.md`](../../resource_info/cutscene_script.md)。

前作 FD2 沒有對應的格式（FD2 的章節事件寫在 `FDFIELD.DAT` 裡，由程式碼驅動），所以沒有可沿用的解碼器；容器解析直接 import [`vfs_dump`](../vfs_dump/_index.md) 的 `parse_container`。

## `cutscene_script.py`

```
python tools/cutscene_script/cutscene_script.py show   <遊戲目錄> <成員名> [--text | --text-renderer 檔案.py:函式]
python tools/cutscene_script/cutscene_script.py dump   <遊戲目錄> <輸出目錄> [--text | --text-renderer 檔案.py:函式]
python tools/cutscene_script/cutscene_script.py verify <遊戲目錄>
python tools/cutscene_script/cutscene_script.py report <遊戲目錄>
```

遊戲目錄是放 `ICONANI.VFS` 與 `FIELD.VFS` 的地方（`fdps_game_files/`）。呼叫端由 `src/` 掃出來：每個 `fdps_icon_script_run(巨集)` 呼叫點、它所在的章節處理函式、巨集的字串值。

- `show`：一支腳本的 Markdown 步驟表印到 stdout。
- `dump`：輸出目錄照慣例用 `workspace/cutscene_script/`。全部 66 支，每支一份 `scripts/<名>.md` 與 `scripts/<名>.json`，外加總表 `index.json`；寫完接著跑 `verify`。
- `verify`：硬指標。每支腳本都要以 opcode `0x00` 結束在最後一個 byte、每條文字引用都落在該區塊的條目數內、每個切換目標都有地圖資料與文字區塊、每個 `PLAY_SAF` 的成員都存在、每支腳本都有呼叫端。任何一項不過就回非零。原版本來就會做的越界寫入另外列出，不算失敗。
- `report`：知識庫用的兩張表（opcode 使用統計、腳本→地圖→文字區塊對照）。

## 給其他工作用的介面

直接 import，不要複製：

```python
sys.path.insert(0, "tools/cutscene_script")
import cutscene_script as cs
reports = cs.decode_all("fdps_game_files", cs.scan_callers())
```

每個 `report`（`ScriptReport`）有：

| 欄位 | 內容 |
| --- | --- |
| `member` | 成員名，大寫（`ICON00.DAT`） |
| `callers` | `Caller` 列表：`function`、`chapter`（章號）、`kind`（`init`／`end`／`event`）、`source`（`src/檔:行`） |
| `initial_map` | 起始地圖編號 |
| `units_known` | 起始時整個單位陣列是否已知（開場腳本是；勝利與戰鬥中事件腳本只知道前段） |
| `steps` | `Step` 列表：`offset`、`opcode`、`mnemonic`、`length`、`raw`、`operands` |
| `trace.steps[i].context` | 該步的 `map`；`DRAW_TEXT` 的 `text`（`{"block", "entry"}`）；`ASK_THREE_WAY` 的 `texts`；`DEPLOY_WAVE` 的 `deployed`（部署記錄列表）；帶單位運算元的步驟的 `units`（索引 → `{"party_slot"}`、`{"map", "record", "char_id"}`、`None` 表示取決於戰況、`"out_of_range"`）；`notes` |
| `trace.switches` | `SWITCH_MAP` 的目標，依序 |
| `trace.maps_visited`、`trace.final_map` | 經過的地圖（不重複）、結束時的地圖 |
| `trace.text_refs` | 全部文字引用 `{"offset", "block", "entry"}`，依序 |
| `trace.final_units` | 結束時的單位陣列（`UnitArray`：`known` 已知的前段、`tail_open` 尾段是否取決於戰況） |
| `trace.problems` | 腳本與出貨資源對不上的地方（出貨資料為空，`verify` 以此判失敗） |
| `trace.findings` | 原版會做的越界單位寫入 |

`dump` 寫出的 JSON 是同一份資料的序列化，欄位名相同。

### 接上文字解碼

文字預設只標成 `FDETXTbb#0xee`（區塊、條目）。要顯示文字內容，傳一個 `renderer(block_no, entry_no) -> str | None` 給 `describe()`／`render_markdown()`；回 `None` 的條目照舊只顯示引用。

- `text_decode_renderer(遊戲目錄)` 回傳一個以 [`text_decode`](../text_decode/_index.md)（文字區塊的擁有者）實作的 renderer，輸出與 `text_decode` 相同，字模對照表還沒有字的字模顯示成 `{glyph 0xNNNN}`。命令列是 `--text`。`text_decode` 只在要求時才 import，沒有它本工具照樣能跑。
- 其他實作用 `--text-renderer 某檔.py:函式` 接上。

## 測試

```
python -m unittest tools/cutscene_script/test_cutscene_script.py
```

`decode_script` 與 `trace_script` 兩個介面各有手算期望值的測試；另一組在 `fdps_game_files/` 存在時跑出貨的 66 支腳本，釘住「全部剛好結束在最後一個 byte」與已知的越界寫入、邊界略過清單。
