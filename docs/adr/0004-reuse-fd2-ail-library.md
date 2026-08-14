# 沿用前作抽出的 AIL 靜態庫，不從 FDPS.LE 重抽

直接連結前作 FD2 專案從 `FD2.LE` 抽出的 Miles AIL 靜態庫，不重複執行一次「從 `FDPS.LE` 抽取 vendor library」的流程。

依據是 `FDPS.LE` 內部的證據——不是外部 driver 檔：主程式內含版本字串 `"3.02"`，且由兩邊 `AIL_startup` body 內同一位移（+0xd3）參照；110 條 `AIL_*` debug 字串表在兩個 binary 中位元組完全相同（各 3916 bytes，SHA-256 相符）。逐一比對 428 個 AIL function，394 個（92.1%）body 位元組相同，其餘 30 個的差異全部是編譯器 codegen 層級（暫存器配置對調、分支成形、整數提升形式），沒有任何語意改動，且差異率嚴格隨函式大小遞增——這是「同源碼不同編譯器」的指紋，不是版本差異。

## Considered Options

前作為了這件事投入了完整的 vendor library 抽取階段，處理了 422 個 function 與上千筆 fixup 判定。對 FDPS 重跑一次是保守但昂貴的選項。

之所以能省掉，關鍵在於 ADR-0001 只要求功能等價。沿用前作的庫做不出 byte-identical 的結果（那 30 個 function 約 12,700 bytes 會與原版不同），在 byte-exact 的目標下這會是致命問題，在功能等價的標準下則無關緊要。

## Consequences

若實際連結時失敗，退路是用前作的抽取 pipeline 對 `FDPS.LE` 重抽——pipeline 本身可沿用，只是要重跑。

兩處尚未驗證的風險：4 個 body 完全由重定位欄位構成、無法判定的 thunk；以及 mixer dispatch table 的 slot 內容未逐一核對。實際連結前需要確認這兩處。
