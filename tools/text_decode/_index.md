# text_decode — 文字區塊解碼器

把 `FIELD.VFS` 的 `FDETXTnn.TXT` 解成逐筆的可讀文字，控制碼以標籤標出。格式見 [`resource_info/text.md`](../../resource_info/text.md)，字模索引到字的對照讀 [`assets/text/glyph_table.json`](../../assets/text/glyph_table.md)。

需要文字內容的工作一律 import 這支，不另寫解析（見 [`tools/_index.md`](../_index.md) 的擁有者表）。

## 用法

```
python tools/text_decode/text_decode.py show workspace/vfs_dump/FIELD/FDETXT01.TXT          # 逐筆印出
python tools/text_decode/text_decode.py show workspace/vfs_dump/FIELD/FDETXT01.TXT --json   # 含 token 明細
python tools/text_decode/text_decode.py all  workspace/vfs_dump/FIELD workspace/text_decode  # 66 個區塊全解
```

`all` 每個區塊寫一份 Markdown 與一份 JSON，外加 `summary.json`；只要有任何字模索引在對照表裡沒有字、或超出字模表，就以非零結束並列出來。

## 程式介面

```python
sys.path.insert(0, "tools/text_decode")
from text_decode import parse_block, render_entry, load_glyph_table

table = load_glyph_table()                      # {字模索引: 字}
for entry in parse_block(data):                 # data = 一個 FDETXTnn.TXT 的 bytes
    entry.index, entry.offset, entry.tokens     # tokens: Token(kind, value, operand, offset)
    render_entry(entry, table)                  # 一筆一行的字串
```

`Token.kind` 是 `glyph`、`line_break`、`page_break`、`subst_1`、`subst_2`、`number`、`speaker_char`、`speaker_unit` 之一；兩種說話者代碼的第二個 word 放在 `operand`。結尾的 -1 不是 token。

## 輸出標籤

| 標籤 | token | 意義 |
| --- | --- | --- |
| 字 | `>= 0` | 對照表裡有字的字模 |
| `{glyph 0xNNNN}` | `>= 0` | 對照表裡還沒有字的字模 |
| `{br}` | -2 | 換行 |
| `{page}` | -3 | 換頁（等按鍵、重畫訊息框） |
| `{subst1}`／`{subst2}` | -4／-5 | 代入全域文字的某一筆，由執行期決定 |
| `{number}` | -6 | 代入執行期的數字 |
| `{speaker char=n}` | -0x11 n | 換說話者，`n` 是角色 id |
| `{speaker unit=n}` | -0x12 n | 換說話者，`n` 是地圖單位索引 |

一筆輸出成單獨一行，換行也只以 `{br}` 表示，方便直接放進 Markdown 表格。

## 測試

`python -m unittest tools/text_decode/test_text_decode.py`：以手工組出的區塊驗證 `parse_block` 與 `render_entry`。
