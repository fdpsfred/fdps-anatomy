# vfs_dump — VFS 容器解包

把 `.VFS` 容器裡的每個成員取出成獨立檔案，並在過程中驗證容器的自洽性。格式與內容清單記在 [`resource_info/vfs.md`](../../resource_info/vfs.md)。

## `vfs_dump.py`

三個子命令：

```
python tools/vfs_dump/vfs_dump.py list   <容器>
python tools/vfs_dump/vfs_dump.py dump   <容器或目錄> <輸出目錄>
python tools/vfs_dump/vfs_dump.py report <manifest json>
```

`dump` 接受單一容器或一個目錄（取其中全部 `.vfs`），把成員以原本的 8.3 名稱寫到 `<輸出目錄>/<容器主檔名>/`，並寫出一份 `manifest.json`，記錄每個容器的 header 欄位與每個成員的名稱、偏移、大小、SHA-256。標準跑法是：

```
python tools/vfs_dump/vfs_dump.py dump fdps_game_files workspace/vfs_dump
```

`list` 印單一容器的成員表，`report` 把 manifest 轉成 `resource_info/vfs.md` 內容清單那兩組 Markdown 表格。

## 巢狀容器

成員自己是容器時（`MISC.VFS` 內有兩個），除了照常寫出成員檔案，還會多開一個 `<成員名>.d/` 目錄把內層成員也解出來。`.d` 後綴不會和成員名撞名，因為解析器只收嚴格的 8.3 名稱，而 `X.VFS.d` 有兩個點。

## 欄位對不上就中止

每個欄位都對照容器自身檢查：magic、版本、簽章、entry table 偏移、筆數上限、名稱的 NUL 結尾與其後的補 0、重複的大小欄位、保留 byte、成員偏移是否等於前一筆的結尾、末筆是否恰好結束於檔尾。任何一項不符就中止並印出是哪個容器的哪一筆的哪個欄位，不猜測也不跳過。

名稱另外要求是嚴格的 8.3 且不是 DOS 裝置名。成員名稱會直接接到輸出目錄後面成為路徑，`C:BOOT.INI` 這種帶磁碟機的名稱在 Windows 上會脫離輸出目錄解析到 C: 磁碟機的工作目錄，`NUL` 則會開到裝置而把內容默默丟掉。

這道檢查是這個工具唯一的正確性依據。容器沒有 checksum，程式自己也不驗 magic，所以「解得出東西」不代表解對了——偏移少算一個 byte 同樣會產出 1,190 個大小正確的檔案，只是每一個都錯位。要求成員首尾相接並精確填滿整個容器，才能把這種錯誤擋下來。
