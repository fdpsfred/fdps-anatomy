# cd_scope — 光碟盤點

把光碟映像的內容透過 DOSBox-X 複製出來，再清點檔案與音軌。結論記在 `resource_info/disc_images.md`。

## `inventory_discs.py`

兩個子命令：

```
python tools/cd_scope/inventory_discs.py copy   <cue 路徑> <輸出目錄>
python tools/cd_scope/inventory_discs.py report <cue 路徑> <輸出目錄> <json 路徑>
```

`copy` 產生一份臨時 DOSBox-X conf，把 `.cue` 以 `imgmount -t cdrom` 掛成 E:、輸出目錄 `mount` 成 F:，在 DOS 內用 `XCOPY /s /e` 複製整片光碟後結束，執行完刪掉 conf。以 `-silent -exit` 執行，全程無需人工介入。

`report` 對複製出來的檔案算大小與 SHA-256，音軌起訖讀 `.cue` 的 TRACK/INDEX 記錄，最後一軌的長度由 `.bin` 檔案大小推得，一併寫成 JSON。

兩個子命令都會拿複製結果比對直接從映像讀出的 ISO9660 根目錄，缺檔或大小不符就中止。這道檢查不能省：DOSBox-X 無論 `IMGMOUNT` 與 `XCOPY` 成敗都回傳 0，少了它，掛載失敗會產出一份格式正確但內容殘缺的清單。`.cue` 只接受單一 `FILE` 指令與 2352 byte 的磁區，其餘型態直接報錯而不是算出錯誤的音軌長度。

複製出來的光碟內容只是中間產物，清點完即可刪除。
