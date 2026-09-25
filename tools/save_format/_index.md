# save_format — `FDE.SAV` 讀寫工具

依 [`resource_info/save.md`](../../resource_info/save.md) 的佈局、檢查碼與加密，把存檔解成看得懂的欄位、驗證檢查碼，並能把改過的明文映像封裝回遊戲讀得了的檔案。

## `fde_sav.py`

```
python tools/save_format/fde_sav.py dump    <FDE.SAV>              每個欄位印成 JSON
python tools/save_format/fde_sav.py verify  <FDE.SAV>...           重算檢查碼並做封裝往返
python tools/save_format/fde_sav.py decrypt <FDE.SAV> <plain.bin>  寫出解密後的明文映像
python tools/save_format/fde_sav.py seal    <plain.bin> <FDE.SAV>  算檢查碼、加密、寫出
```

`verify` 對每個檔做兩件事：解密後重算檢查碼、與 `+0x59c7` 存的值比；再把解密結果照遊戲的順序（先檢查碼、後加密）封裝一次，要與原檔逐 byte 相同。任一不符就印 `FAIL` 並以 1 結束。

`dump` 的 roster 與地圖單位只列到標頭給的筆數；超過筆數的 byte 是先前寫入留下的殘值，不解讀。章節 byte 是 `0xff` 的 slot 與 live-state 只印出這個標記，其餘欄位同樣是殘值、不解讀。檢查碼不符時照樣解出來並標 `checksum_ok: false`，因為遊戲自己也照樣讀入。

改存檔的流程是 `decrypt` → 用任何十六進位編輯器改明文 → `seal`。`seal` 只接受恰好 `0x59cb` byte 的輸入。

## 測試

```
python -m unittest tools/save_format/test_fde_sav.py
```

預期值取自與實作無關的來源：密鑰流前三個 byte 是依 `000568c9`／`000568ce` 兩道指令手算的 `cc 00 a1`，欄位偏移取自 `src/` 裡讀寫它們的指令，另有兩項對 `fdps_game_files/FDE.SAV` 的實檔驗證（檔案不在時略過）。
