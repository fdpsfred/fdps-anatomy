# 11 — 建置旗標組與預設 calling convention 判定

**What to build:** 從 binary 反推出當年建置這個執行檔所用的編譯器與連結器旗標組——記憶體模型、浮點模型、預設 calling convention、堆疊大小等。

注意這裡判定的是**預設值**。個別 function 的 calling convention 會有差異，那些必須在 emit 時於程式碼中明確宣告，不由旗標決定；逐 function 的 cc 判定屬於 15 號票的範圍。

**Blocked by:** 10

**Status:** done

- [x] 從 binary 特徵反推記憶體模型、浮點模型、預設 calling convention
- [x] 連結器旗標組（含堆疊大小、目標格式）確認，參考前作的反推方法
- [x] 判定依據逐項記錄證據，不是猜測
- [x] 明確記錄「個別 function 的 cc 會有差異、必須在程式碼中明確宣告」這條規則，以及如何辨識偏離預設的 function
- [x] 旗標組與判定依據進知識庫

結論：`wcc386 -bt=dos4g -mf -4s -fpi -s -od`，連結 `system dos4g` + `option stack=8k` +
`clib3s/math387s/emu387`，工具鏈是 Watcom 10.0a（10.0b 無法排除）。
判定依據見 `rebuild_info/build_flags.md`，重現腳本見 `tools/build_flags/`。
