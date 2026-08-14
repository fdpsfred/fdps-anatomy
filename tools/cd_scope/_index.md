# cd_scope — 光碟盤點

把光碟映像的內容透過 DOSBox-X 複製出來，再清點檔案與音軌。結論記在 `resource_info/disc_images.md`。

## `inventory_discs.py`

兩個子命令：

```
python tools/cd_scope/inventory_discs.py copy   <cue 路徑> <輸出目錄>
python tools/cd_scope/inventory_discs.py report <cue 路徑> <輸出目錄> <json 路徑>
```

`copy` 產生一份臨時 DOSBox-X conf，把 `.cue` 以 `imgmount -t cdrom` 掛成 E:、輸出目錄 `mount` 成 F:，在 DOS 內用 `COPY` 複製整片光碟後結束，執行完刪掉 conf。以 `-silent -exit` 執行，全程無需人工介入。

`report` 對複製出來的檔案算大小與 SHA-256，音軌起訖讀 `.cue` 的 TRACK/INDEX 記錄，最後一軌的長度由 `.bin` 檔案大小推得，一併寫成 JSON。

複製出來的光碟內容只是中間產物，清點完即可刪除。
