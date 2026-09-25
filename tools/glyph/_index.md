# glyph — 字模對照表

替 `FDETXT.FON` 的每個字模找出它代表的字，產生 [`assets/text/glyph_table.md`](../../assets/text/glyph_table.md) 與機器可讀的 `glyph_table.json`。字模表格式見 [`resource_info/text.md`](../../resource_info/text.md)。

做法沿用前作：`fd2-anatomy/tools/glyph/et3_pixel_match.py`（倚天字型的索引佈局與逐像素比對）與 `fd2-anatomy/resource_info/chinese_glyph_encoding.md`（倚天字序為何不是 Big5 線性公式）。`ET3_fonts/` 的兩個字型檔從 `fd2-anatomy/tools/glyph/ET3_fonts/` 逐 byte 複製而來。

## 流程

```
python tools/vfs_dump/vfs_dump.py dump fdps_game_files workspace/vfs_dump   # 取出 FDETXT.FON 與 FDETXTnn.TXT
python tools/glyph/glyph_match.py                                            # -> workspace/glyph/match.json
python tools/glyph/review_page.py                                            # -> workspace/glyph/review/glyph-review.html
（開發者在網頁上填字；答案存在網頁的資料庫 answers 集合）
python tools/glyph/glyph_table.py import <答案傾印目錄>                      # -> tools/glyph/developer_answers.json
python tools/glyph/glyph_table.py build                                      # -> assets/text/glyph_table.{json,md}
```

| 檔案 | 作用 |
| --- | --- |
| `glyph_match.py` | 每個字模對倚天 `STDFONT.15`（與 `ASCFONT.15`）逐像素比對；完全相同的直接得字，其餘列出最接近的候選與差異像素數，外加前作 FD2 字模表中最接近的字模與前作給它的字。同時對前作做交叉驗證：與 FD2 字模逐 byte 相同、又與倚天完全相同的字，兩邊的答案必須一致 |
| `review_page.py`、`review_page_template.html` | 把不完全吻合的字模做成校對網頁：放大圖、候選、出現的文字片段、輸入框 |
| `glyph_table.py` | `import` 把網頁資料庫的答案傾印轉成 `developer_answers.json`；`build` 合併完全吻合的字與開發者的答案寫出對照表，還有待填的字就以非零結束 |
| `developer_answers.json` | 開發者在校對網頁上填的字。人工輸入、不可重生，所以與腳本一起進版控 |
| `ET3_fonts/` | 倚天 `STDFONT.15`（13,094 個 16×15 字模）與 `ASCFONT.15`（256 個 8×15 字模） |
| `test_glyph_match.py` | 倚天索引佈局的錨點與「完全吻合」判定的測試：`python -m unittest tools/glyph/test_glyph_match.py` |
| `test_glyph_table.py` | 答案規則（一格一字、兩格一字重複兩次的例外）的測試：`python -m unittest tools/glyph/test_glyph_table.py` |

## 「完全吻合」的定義

16×16 的整格與倚天某個 15 列字模放在第 0 列起或第 1 列起的樣子逐像素相同，剩下的那一列必須全空。只比 15 列、不檢查剩下那一列的做法會把那一列有像素的字模也算成吻合。全空白的字模不算吻合。

## 答案怎麼讀回

校對網頁宣告了資料庫能力，每填一格就寫一份 `answers/g<四位十六進位索引>` 文件，內容 `{index, char}`。網頁只接受一個字；打字途中（輸入法還在組字）不寫入；寫入失敗或離線時的答案留在瀏覽器裡，連上後自動補寫。

讀回用 ArtifactData 的 `list`：`collection: "answers"`、`query: {limit: 1000}`（一頁就涵蓋全部）、`out_dir: workspace/glyph/answers_dump`，它把每份文件寫成 `<out_dir>/answers/g<索引>.json`。再跑 `glyph_table.py import workspace/glyph/answers_dump`：答案不是恰好一個字就中止，還有沒答的字就列出來並以非零結束。整段不經過任何手動轉貼。

## 一格兩個符號的例外

`0x00C4` 與 `0x0196` 在一個字模裡並排畫了兩個相同的符號（兩個問號、兩個驚嘆號），對照表裡各對到兩個字元。例外清單寫死在 `glyph_table.py` 的 `DOUBLE_SYMBOL_GLYPHS`，只放行這兩個索引、而且必須是同一個字元重複兩次；網頁上這兩格仍只填一個字元，`import` 讀回時自動重複成兩個。其他索引一律恰好一個字元。
