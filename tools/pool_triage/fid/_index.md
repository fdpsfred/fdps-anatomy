# tools/pool_triage/fid — 函式庫比對

把靜態程式庫拆成 OMF module、做成 Ghidra Function ID 資料庫，再對 `FDPS.LE` 查詢，回答「這段機械碼來自哪個程式庫的哪個模組」。這是唯一能直接證明程式碼來源的手段，pool 判定的其他三類證據（連結方向、字串、共用狀態）都是間接的。

比對的對象有兩組，共用同一條管線：

| 對象 | 來源 | 證明什麼 | 用在哪 |
| --- | --- | --- | --- |
| Watcom 執行期 | `WATCOM_10.0`／`10.0a`／`10.0b` 的 `CLIB3S`、`MATH387S`、`EMU387`、`GRAPH`、`CSTRTX3S` | 這段程式碼就是該版 Watcom 程式庫的那個模組 | 票 14 的 `crt` 判定 |
| Miles AIL | 前作 `fd2-anatomy/libs/ailv3/ailv3.lib` | 這段程式碼與 `FD2.LE` 的那一段位元組相同 | 票 14.1 的 `ail` 覆核 |

**AIL 這一組證明的不是官方發行版。** `ailv3.lib` 是前作從 `FD2.LE` 的位元組合成出來的，不是 Miles 的 `.LIB`。命中代表兩個 binary 的這段程式碼同源，不代表與任何一個官方 Miles build 相同——這正好是 [ADR-0004](../../../docs/adr/0004-reuse-fd2-ail-library.md) 要回答的問題。

## 檔案

| 檔案 | 用途 |
| --- | --- |
| `extract_libs.py` | 用 `wlib -q -x` 把三個版本的 Watcom 程式庫拆成 `.obj`，修 quirky record，建去重清單與扁平目錄 |
| `extract_ail_lib.py` | 把 `ailv3.lib` 拆成 `ail_code.obj` 與 `ail_data.obj`，同樣過一次 quirky record 檢查 |
| `omf_patch_segdef.py` | 修 Watcom Easy OMF-386：型別是 16-bit 但欄位是 32-bit 的 record，Ghidra 的 OmfLoader 讀不了 |
| `build_manifest.py` / `build_dedup_dir.py` | 以 SHA-256 去重，替每個唯一模組取一個穩定檔名 |
| `FidWipeFolder.java` / `FidImportBatch.java` / `FidAnalyzeAll.java` | 清空、匯入、分析 Ghidra 專案裡的程式庫模組 |
| `FidDemoteAltEntries.java` | 修 `ail_code.obj` 的 function 邊界：把 mid-function alternate entry 降回 label，重建被它切斷的 function |
| `FidPopulate.java` | 逐 Watcom 版本建出 `.fidb` |
| `FidPopulateAil.java` | 替 AIL 建出單一 `.fidb`，語言由模組決定而非由參數指定 |
| `FidQuery.java` | 對 `FDPS.LE` 查詢，輸出每個位址的候選符號與分數 |
| `FidHashDump.java` | 匯出每個 function 的 FID 雜湊與 code unit 數，用來分辨「沒有對應模組」與「短到問不出問題」 |
| `DumpCallerCounts.java` | 匯出每個 function 的外部參照數，用來分辨「程式庫少了這一段」與「少了一段死碼」 |
| `analyze_ail_matches.py` | 把 AIL 查詢結果換算成涵蓋率、與 pool 判定不一致的個案、兩邊 function 數的對帳 |
| `build_contradiction_worklist.py` | 替每個不一致的 function 攤一份證據包 |
| `reread_contradictions.js` | 票 14.1 的 workflow：一個不一致一個 agent，重判、落地、跑 gate、回掃 |
| `extract_watcom_symbols.py` | 用 `wlib -l` 取出實際連結的四個程式庫的全部公開符號，命名的對照基準 |
| `build_symbol_renames.py` | 算出哪些 function 可以直接改成程式庫的符號名，哪些要交給 agent |
| `ApplyLibrarySymbolNames.java` | 把機械改名寫進 Ghidra，兩段式避免互換名稱時撞名 |
| `sync_verdict_names.py` | 把改名同步回 pool 判定檔，兩邊的記錄不會分岔 |
| `name_disputed_symbols.js` | workflow：一個爭議名稱一個 agent，定名、落地、跑 gate |

## 跑法

Watcom 執行期（Ghidra MCP，依序）：

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

Miles AIL：

```bash
python tools/pool_triage/fid/extract_ail_lib.py
```
```
import_file            ail_code.obj / ail_data.obj → /ail_lib
run_ghidra_script FidAnalyzeAll.java      args: /ail_lib
run_ghidra_script FidDemoteAltEntries.java args: /ail_lib/ail_code.obj
run_ghidra_script FidPopulateAil.java     args: /ail_lib <fidb dir>
run_ghidra_script FidQuery.java           args: <fidb dir> <results dir> 0
run_ghidra_script FidHashDump.java        args: <out.json>        （對 FDPS.LE 與 ail_code.obj 各跑一次）
run_ghidra_script DumpCallerCounts.java   args: <work>\caller_counts.json   （對 FDPS.LE）
```
```bash
python tools/pool_triage/fid/analyze_ail_matches.py
python tools/pool_triage/fid/build_contradiction_worklist.py
```

不一致的個案由 workflow 收尾，一個 function 一個 agent：

```
Workflow({ scriptPath: "tools/pool_triage/fid/reread_contradictions.js", args: { roundSize: 8 } })
```

## 從比對結果取命名

比對的第二個用途是命名。[`rebuild_info/naming.md`](../../../rebuild_info/naming.md) 要求 vendor 的 function 就叫程式庫的名字、不加前綴，而 Function ID 的命中正好就是「這一段等於程式庫的那一個 function」——所以命中本身就是名字的來源，不需要另外造一套。

```bash
python tools/pool_triage/fid/extract_watcom_symbols.py
python tools/pool_triage/fid/build_symbol_renames.py --symbols workspace/pool_triage/fid/watcom_symbols.json
```
```
run_ghidra_script ApplyLibrarySymbolNames.java  args: <renames>\mechanical.json          （先跑一次不帶 apply 看清單）
run_ghidra_script ApplyLibrarySymbolNames.java  args: <renames>\mechanical.json apply
```
```bash
python tools/pool_triage/fid/sync_verdict_names.py
```
```
Workflow({ scriptPath: "tools/pool_triage/fid/name_disputed_symbols.js", args: { roundSize: 5 } })
```

`build_symbol_renames.py` 只在證據唯一時才自己決定：body 位元組相同、只有一個候選、而且那個名字沒有第二個人在爭。**任何一個名字被兩個位址claim 就整組送進 workflow**——衝突代表其中一邊的識別是錯的，在這裡挑一個贏家就是 ADR-0002 禁止的批次判斷。實測第一輪 461 個裡有 446 個是機械轉換，15 個要逐一讀過；後續又從殘留的命名偏差（`_impl` 後綴）、5-byte thunk 與撞名補進 14 個，逐一定名的共 29 個。

`run_ghidra_script` 的 `script_name` 傳這裡的**完整路徑**即可，它會自己複製到 Ghidra 的腳本目錄；傳短名字只有在該檔已經在那個目錄裡才找得到。另外它作用在 MCP 當下的 current program，而這條管線會把程式庫模組匯進同一個專案，所以**每一次呼叫都要明寫 `program`**。

分數只是證據不是結論：短 function 會互撞，兩條指令的 stub 能同時對上幾十個程式庫模組。每個 function 仍然由一個 agent 讀過 assembly 才定案。

## 已知的坑

**匯入的程式名稱就是檔名，所以要先把去重後的模組攤成 `<key>.obj`。** `FidPopulate` 是用 manifest 的 key 去找 Ghidra 裡的程式；直接匯入 `wlib` 抽出來的 `<module>.obj` 會讓不同版本的同名模組撞成 `xxx.obj.0`、`xxx.obj.1`，`FidPopulate` 一個都找不到，症狀是 `programs: 0  missing(import-failed): 732`。

**`font8x8.obj` 匯不進來**（`Unable to read past EOF`），這是 Ghidra OmfLoader 的已知問題。它是 GRAPH.LIB 的 CP437 8×8 字型，FDPS 用自己的中文字型，比對上不受影響。

**Easy OMF-386 的 quirky record 一定要先修。** `emu387.lib` 與 GRAPH 的 11 個模組都是這種形式，不修就是匯入失敗或註冊出幽靈符號。`ailv3.lib` 的兩個模組不是——前作的 OMF writer 直接寫 32-bit record 型別，所以 `omf_patch_segdef.py` 對它是零改動。

**傳給 `FidPopulate` 的 languageID 必須等於模組的 languageID，不然整批被靜默跳過。** `FidServiceLibraryIngest` 對每個程式做一次 `languageId.equals(program.getLanguageID())`，不等就 `continue`，最後回傳 `null`；症狀是 `createNewLibraryFromPrograms` 的結果為 null 而不是任何錯誤訊息。`FidImportBatch` 是靠匯入後呼叫 `setLanguage` 才讓 Watcom 那批統一成 `x86:LE:32:watcom` 的；用 `import_file` 直接匯入的 AIL 模組停在 `x86:LE:32:default`，所以 `FidPopulateAil` 改成從模組讀語言。查詢端不受影響：`FidFile.canProcessLanguage` 是用 `ProcessorSizeComparator` 比 language description，只看處理器與位元數。

**`ail_code.obj` 的 mid-function alternate entry 會讓 function 邊界對不上。** 前作的 OMF emitter 替每個被引用的函式中途位址發一個 `L_<函式>_alt_<位移>` 符號，Ghidra 的自動分析把每個都當成 function 起點，含有它的那個 function 的 body 就被切短了——切短的 body 雜湊值與 `FDPS.LE` 裡完整的同一段不同，於是安靜地比不上。`FidDemoteAltEntries.java` 把它們降回 label 再重建 function；重建走位址遞減順序，後一個 function 先存在，前一個才不會把它吃掉。有一小部分 alternate entry 只被別的 function 跳進來，降級後沒有任何 function 涵蓋它的程式碼，這些會被還原成 function——判準是涵蓋率，不是猜。這一步在本專案上把整份 binary 的命中位址數從 386 拉到 397，代價是 `AIL_sample_playback_rate` 反過來變成沒命中（合併之後 lib 這邊 224→241，`FDPS.LE` 那邊 Ghidra 切在 224）——邊界沒有一個絕對正確的畫法，兩邊對齊到同一種畫法才有意義。
