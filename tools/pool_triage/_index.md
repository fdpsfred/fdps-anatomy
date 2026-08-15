# pool_triage — 未辨識區塊與 pool 判定

把 `.object1` 裡每一段還沒被辨識的位元組判定完，再替程式裡每一個 function 回答「它屬於哪個 pool」。這是票 14 專屬的 workflow，不是給別票用的框架（[ADR-0007](../../docs/adr/0007-workflow-automation-and-agent-context.md)）。

工作分兩階段，順序不能對調：**先把 function 集合補齊，pool 判定的清單才算數**。連結器帶進來但呼叫圖走不到的程式碼從來沒被自動分析碰過，不先建成 function，判 pool 就會漏掉程式庫區的大半。

| 檔案 | 用途 |
| --- | --- |
| `triage_ticket14.js` | 主 workflow：判區塊 → 建 function → 判 pool → 回掃 → 寫知識庫與 devlog，全程無人介入 |
| `DumpTriageWorklist.java` | 匯出兩份工作清單（未定義區塊、全 function）與 `.object1` 的映像，唯讀 |
| `build_worklists.py` | 把匯出結果攤成「一個工作項目一個證據包」，並算出還沒判定的清單 |
| `ApplyBlockTriage.java` | 把區塊判定轉錄進 Ghidra：建 function、定資料型別、對齊填充定成 byte |
| `ApplyPoolVerdicts.java` | 把 pool 判定轉錄進 Ghidra：function tag、plate comment、程式庫符號名 |
| [`fid/`](#fid--watcom-程式庫比對) | Watcom 程式庫比對，產出 CRT 判定的證據 |

## 執行

```
Workflow({ scriptPath: "tools/pool_triage/triage_ticket14.js",
           args: { stage: "all", maxBlocks: 900, maxFunctions: 900, roundSize: 40 } })
```

`stage` 可以是 `blocks`、`pools` 或 `all`。**可重跑**：已經有判定檔的項目不會再判一次，所以受 agent 預算限制而中止的一輪，下次接著跑就好。

## 兩個撐住整個設計的性質

**一次一個項目。** 工作清單只存在於檔案與腳本的迴圈裡，每次 `agent()` 呼叫只帶一個區塊或一個 function，清單從頭到尾沒有交給任何一個 agent（[ADR-0002](../../docs/adr/0002-no-batch-processing-per-function.md)）。

**證據先攤好，context 才有界。** `build_worklists.py` 事先把每個項目需要的東西——位元組、前後鄰居、呼叫邊、程式庫比對命中——寫成一個證據包。判一段對齊填充因此只要一次 `Read`、零次 Ghidra 呼叫。判定寫成檔案、只回傳約 200 byte 的摘要，所以腳本與後續每一段的 context 都不隨處理量成長。

## 各階段

| 階段 | 做什麼 | 一次處理幾個 |
| --- | --- | --- |
| Plan | 重跑匯出、重建證據包、讀出還沒判定的清單 | — |
| Blocks | 讀一個未定義區塊，判定是填充、資料還是程式碼 | 1 |
| ApplyBlocks | 把該輪判定轉錄進 Ghidra 並跑基準稽核 gate | 一輪 |
| Pools | 讀一個 function 的 assembly，判定 pool 歸屬 | 1 |
| ApplyPools | 打 pool tag、寫 plate comment、必要時改名，跑 gate | 一輪 |
| Rescan | 重讀一個尚未定案的判定，這次可引用鄰居的判定檔 | 1 |
| Report | 寫 `program_info/code_pools.md`、票 15 的工作清單與 devlog | — |

## 區塊階段為什麼要跑好幾遍

一個未定義區塊常常不只裝一個 function——`0x3f6d4` 那段有 3,448 byte。要求單一 agent 把整段切乾淨是逼它猜，所以判定檔有 `covered_to` 欄位：agent 只認它讀得懂的那一段，剩下的照實留著。每輪落地後 workflow 重跑匯出，沒被吃掉的尾巴就會以一個新的、比較小的區塊回到清單裡，下一遍再判。某一遍沒有新建任何 function 就停。

## fid — Watcom 程式庫比對

CRT pool 的判定要有函式庫佐證，做法沿用前作 FD2 的 Ghidra Function ID pipeline，不自己寫 OMF parser。

| 檔案 | 用途 |
| --- | --- |
| `extract_libs.py` | 用 `wlib -q -x` 把 10.0／10.0a／10.0b 的 `CLIB3S`、`MATH387S`、`EMU387`、`GRAPH`、`CSTRTX3S` 拆成 `.obj`，修 quirky record，建去重清單與扁平目錄 |
| `omf_patch_segdef.py` | 修 Watcom Easy OMF-386：型別是 16-bit 但欄位是 32-bit 的 record，Ghidra 的 OmfLoader 讀不了 |
| `build_manifest.py` / `build_dedup_dir.py` | 以 SHA-256 去重，替每個唯一模組取一個穩定檔名 |
| `FidWipeFolder.java` / `FidImportBatch.java` / `FidAnalyzeAll.java` | 清空、匯入、分析 Ghidra 專案裡的程式庫模組 |
| `FidPopulate.java` | 逐版本建出 `.fidb` |
| `FidQuery.java` | 對 `FDPS.LE` 查詢，輸出每個位址的候選符號與分數 |

跑法（Ghidra MCP，依序）：

```bash
python tools/pool_triage/fid/extract_libs.py
```
```
run_ghidra_script FidWipeFolder.java   args: /watcom_libs
run_ghidra_script FidImportBatch.java  args: <manifest> /watcom_libs 0 <n>
run_ghidra_script FidAnalyzeAll.java   args: /watcom_libs
run_ghidra_script FidPopulate.java     args: <manifest> /watcom_libs <fidb dir> x86:LE:32:watcom
run_ghidra_script FidQuery.java        args: <fidb dir> <results dir> 0
```

分數只是證據不是結論：短 function 會互撞，兩條指令的 stub 能同時對上幾十個程式庫模組。每個 function 仍然由一個 agent 讀過 assembly 才定案。

## 已知的坑

**匯入的程式名稱就是檔名，所以要先把去重後的模組攤成 `<key>.obj`。** `FidPopulate` 是用 manifest 的 key 去找 Ghidra 裡的程式；直接匯入 `wlib` 抽出來的 `<module>.obj` 會讓不同版本的同名模組撞成 `xxx.obj.0`、`xxx.obj.1`，`FidPopulate` 一個都找不到，症狀是 `programs: 0  missing(import-failed): 732`。

**`font8x8.obj` 匯不進來**（`Unable to read past EOF`），這是 Ghidra OmfLoader 的已知問題。它是 GRAPH.LIB 的 CP437 8×8 字型，FDPS 用自己的中文字型，比對上不受影響。

**Easy OMF-386 的 quirky record 一定要先修。** `emu387.lib` 與 GRAPH 的 11 個模組都是這種形式，不修就是匯入失敗或註冊出幽靈符號。
