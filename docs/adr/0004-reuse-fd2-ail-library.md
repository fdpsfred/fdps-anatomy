# 沿用前作抽出的 AIL 靜態庫，不從 FDPS.LE 重抽

直接連結前作 FD2 專案從 `FD2.LE` 抽出的 Miles AIL 靜態庫，不重複執行一次「從 `FDPS.LE` 抽取 vendor library」的流程。

依據是 `FDPS.LE` 內部的證據——不是外部 driver 檔：主程式內含版本字串 `"3.02"`，且由兩邊 `AIL_startup` body 內同一位移（+0xd3）參照；110 條 `AIL_*` debug 字串表在兩個 binary 中位元組完全相同（各 3916 bytes，SHA-256 相符）。逐一比對 428 個 AIL function，394 個（92.1%）body 位元組相同，其餘 30 個的差異全部是編譯器 codegen 層級（暫存器配置對調、分支成形、整數提升形式），沒有任何語意改動，且差異率嚴格隨函式大小遞增——這是「同源碼不同編譯器」的指紋，不是版本差異。

## Considered Options

前作為了這件事投入了完整的 vendor library 抽取階段，處理了 422 個 function 與上千筆 fixup 判定。對 FDPS 重跑一次是保守但昂貴的選項。

之所以能省掉，關鍵在於 ADR-0001 只要求功能等價。沿用前作的庫做不出 byte-identical 的結果（那 30 個 function 約 12,700 bytes 會與原版不同），在 byte-exact 的目標下這會是致命問題，在功能等價的標準下則無關緊要。

## 以 Function ID 獨立覆核

上面的依據來自 body 逐一比對。之後把 `ailv3.lib` 拆成 OMF module 建成 Ghidra Function ID 資料庫、對 `FDPS.LE` 查詢，用另一種方法問同一個問題，結論一致：436 個 `ail` function 有 390 個與程式庫的某個 function 位元組相同（遮掉重定位運算元後），沒有任何一個是「命中但雜湊不同」。剩下 46 個裡有 30 個能與程式庫那些沒有 FDPS 對應的 function 對得起來，body 只差幾個 byte，也就是同一支 function 的不同編譯結果——與 body 比對得到的那 30 個 codegen 層級差異是同一件事，只是換一條路量到。完整數字與每一類的明細屬於 [`program_info/code_pools.md`](../../program_info/code_pools.md)。

**票 14.2 逐 function 重讀全部 1,345 個判定之後，本 ADR 的前提維持成立。** `ail` pool 從 442 縮到 436（六支 DPMI 服務常式改判為遊戲自己的程式碼，那正是前作把它們放進 `fd2common.lib` 而不是 `ailv3.lib` 的同一條界線），命中數 390 與程式庫那側的 428 都沒有變；重讀還把原本沒命中的 46 個裡的 30 個，用模組位置與前作的 fixup 表接回程式庫裡有名字的 function，「兩邊是同一份 AIL」因此比第一遍更有支撐。退路不啟動。

本 ADR 原本列的兩處未驗證風險，一處關閉、一處縮小：

- **mixer dispatch table 的 slot 內容**：關閉。兩張表的 132 個 slot 目標全部命中、全部單一候選、full hash 相同。
- **完全由重定位欄位構成的 thunk**：`FDPS.LE` 這邊有四個（`0003da44`、`0003da49`、`0003de38`、`0003dcb0`）。Function ID 對它們算不出雜湊，但它們的目標可以，而且前作的庫本來就把這種 thunk 當公開符號收著——前三個的目標分別與 `AIL_internal_log_lock_acquire`、`_release`、`AIL_internal_get_isr_lock_count` 的 body 位元組相同，只有 `0003dcb0` 的目標落在下面那批沒有對應的 function 裡。

## Consequences

若實際連結時失敗，退路是用前作的抽取 pipeline 對 `FDPS.LE` 重抽——pipeline 本身可沿用，只是要重跑。覆核沒有推翻本 ADR 的前提，所以這條退路維持在「備而不用」，不啟動。

**但沿用前作的庫不等於 FDPS 的 AIL 就齊了。** 有 16 個 `ail` function 在 `ailv3.lib` 裡完全沒有對應，其中 8 個有呼叫端。這批以 `0003ccf8` 為首的 LX 驅動映像載入層為主，前作的 AIL 沒有這一層。連結階段（票 19）要另外補，做法與清單見 [`rebuild_info/pitfalls.md`](../../rebuild_info/pitfalls.md) 與 [`program_info/code_pools.md`](../../program_info/code_pools.md)。
