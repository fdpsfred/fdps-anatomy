# tools/cut_content — 刪減與未用資料夾的共同入口

[`cut_content/`](../../cut_content/_index.md) 的總表產生、結構閘門與素材重生都從這裡進去。票 25.10 建立骨架，25.11–25.14 各自註冊自己主題的素材產生器。

前作沒有對應成果：`fd2-anatomy` 沒有刪減與未用的資料夾，`fd2-anatomy/tools/rsrc_unresolved` 只是資源死碼的一次性驗證。素材的解碼沿用本專案的擁有者（[`cel_decode`](../cel_decode/_index.md)、[`saf_decode`](../saf_decode/_index.md)、[`vfs_dump`](../vfs_dump/_index.md)），這裡不另寫解碼器。

## 檔案

| 檔案 | 用途 |
| --- | --- |
| `cut_content.py` | 四個子命令（見下）；素材產生器的註冊表 `GENERATORS`；給產生器用的讀檔與寫檔函式 |
| `test_cut_content.py` | 單元測試：`python -m unittest tools/cut_content/test_cut_content.py` |

## 子命令

```
python tools/cut_content/cut_content.py index
python tools/cut_content/cut_content.py check
python tools/cut_content/cut_content.py media <主題> [--game DIR]
python tools/cut_content/cut_content.py verify-media [<主題>...] [--game DIR]
```

- `index`：從五個主題檔的條目標題列與 `分類：` 行產生 `cut_content/_index.md` 的總表，蓋掉表頭相同的舊表。
- `check`：結構閘門。條目標題與分類、編號字首符合主題、編號不重複、同一編號不同時是條目與排除項、總表與主題檔同步、相對連結都存在、沒有引用 `workspace/`、每個素材檔的名稱照規則、屬於一個條目且被主題檔引用。`media/cdda/` 不檢查。每張票改完 `cut_content/` 都要跑到 `OK`。
- `media`：從原版遊戲檔重生一個主題的素材，整個 `cut_content/media/<主題>/` 換成新產生的內容，舊檔不留。
- `verify-media`：重生到暫存資料夾，逐 byte 比對版控中的素材，列出多出、少了與內容不同的檔。「從遊戲檔重生結果相同」由它證明。

`--game` 預設是 `fdps_game_files/`（原版遊戲檔，不進版控）。

## 註冊素材產生器

產生器是 `(out_dir, game_dir) -> None` 的函式，把該主題的全部素材寫進 `out_dir`，登記在 `GENERATORS[<主題>]`。主題鍵是 `code`、`units`、`items`、`battle_assets`、`story`。

- 讀遊戲檔用 `read_game_file(game_dir, "FACE.CEL")` 與 `read_vfs_member(game_dir, "FIGACT.VFS", "STAND087.SAF")`，不要讀 `workspace/vfs_dump/`，那是可刪的中間產物。
- 寫檔用 `write_png(path, width, height, rgba_rows)` 與 `write_wav(path, sound)`，分別轉交 `cel_decode` 與 `saf_decode` 的寫出函式；兩者的輸出是決定性的，重生才會逐 byte 相同。
- 檔名照 [`cut_content/_index.md`](../../cut_content/_index.md)「素材」的規則，`check` 會擋下不合規的名稱。
- CD 音軌的 WAV 寫到 `cut_content/media/cdda/`（`.gitignore` 排除），不經 `media` 子命令的整夾替換。
