# 2026-08-15 未辨識區塊逐一判定（票 14 前半）

票 14 分兩段：先把 `.object1` 裡每一段沒被辨識的 byte 判定完、該建成 function 的建起來，再逐一判每個 function 的 pool。這篇記第一段，跑完的狀態是程式碼 object 內未定義 byte 歸零、function 從 1,042 變 1,347。

## 先撞的牆：自己寫 OMF parser

CRT pool 的判定要有函式庫佐證，第一個念頭是寫一支 OMF reader 把 Watcom 的 `.LIB` 拆開，重建每個模組的 code segment 加上 FIXUPP 遮罩，然後在 `FDPS.LE` 的映像裡做遮罩比對。寫完 parser、`CLIB3S.LIB` 的 394 個模組都解得乾淨、376 個 CODE segment 的 LEDATA 全部覆蓋，看起來很順。

然後 `emu387.lib` 讓它掛了：`IndexError` 在 PUBDEF 的 type index。原因是 Watcom 的 Easy OMF-386——record 型別寫 `0x98`（16-bit SEGDEF）但欄位是 32-bit 寬度。這種 record 混在同一個檔案裡，parser 一路對錯位。

這時使用者指出前作 FD2 已經做過這件事。去看 `tools/program_analysis/crt_fid_match/`，發現三件事：

1. FD2 一開始也是自己寫 parser，後來換成直接呼叫 `wlib -q -x`，理由寫在檔頭：「這是過度設計，wlib 本來就處理所有 Easy OMF-386 的 quirk，而且與 Python parser 逐模組比對 379/379 相同」。
2. Ghidra 的 OmfLoader 讀不了那些 quirky record，FD2 寫了 `omf_patch_segdef.py` 把型別 byte 升成 32-bit 版本再重算 checksum。
3. 主力比對不是自己做的 byte match，而是 Ghidra 的 Function ID：把每個 `.obj` 匯入成獨立 program、跑分析、建 `.fidb`，再對目標查詢。

自己寫的 parser 與 matcher 當場刪掉，改抄 FD2 的 pipeline。這是這個工作段落最划算的一次轉向——如果先問過前作，前面兩小時可以省下來。

## FID pipeline 的坑：程式名稱就是檔名

照抄之後第一次跑 `FidPopulate`，輸出是 `programs: 0  missing(import-failed): 732`。820 個模組明明匯入成功。

原因是 `FidPopulate` 用 manifest 的 `key` 欄位（`<sha12>_<模組名>`）去 Ghidra 專案裡找程式，而 `FidImportBatch` 匯入時程式名稱取自檔名。`wlib` 抽出來的檔案叫 `<模組名>.obj`，三個版本的同名模組還會撞成 `fclose.obj.0`、`fclose.obj.1`。`FidImportBatch` 裡有一段 `df.setName(key)` 想改名，但包在 `catch (Throwable ignore) {}` 裡，失敗了也不會有人知道。

FD2 是靠 `build_dedup_dir.py` 這一步繞過去的：先把去重後的模組複製成 `<key>.obj` 再匯入。抄 pipeline 的時候漏了這支，症狀就變成「匯入 820 個、找到 0 個」。補上之後 `.fidb` 三個版本都建起來，對 `FDPS.LE` 命中 152 個 function。

順帶一提，`font8x8.obj` 匯不進來（`Unable to read past EOF`），與 FD2 遇到的是同一個 Ghidra bug。它是 GRAPH.LIB 的 CP437 字型，FDPS 用自己的中文字型，不影響。

驗證這條管線可信的是 `0x435f3`：FID 說它是 `strupr`，而票 11 早就用 31 byte 無重定位的機械碼比對得到同一個結論。兩條獨立的路走到同一個答案。

## Plan agent 回傳 738 個位址，卡了十幾分鐘

workflow 腳本沒有檔案系統存取，所以工作清單得從別處進來。第一版讓一個 planning agent 去讀 `worklist.json` 再把 738 個位址回傳給腳本。

跑起來之後 transcript 十幾分鐘沒有動靜。agent 沒有卡住，它在一個 token 一個 token 地吐 738 個 8 位十六進位字串——那是一萬多個 output token，而且全程不能做別的事。

改成從 `args` 把清單傳進去。清單本來就在呼叫端手上，繞一圈讓 agent 覆誦一遍只是浪費。後續每一遍的殘餘清單很短，那些仍然讓 refresh agent 回傳。

## 區塊縮小之後 start 不變，會被永遠跳過

大區塊常常裝著好幾個 function，`0x3f6d4` 那段有 3,448 byte。要求單一 agent 把整段切乾淨是逼它猜，所以判定檔有 `covered_to`：agent 只認它讀得懂的那一段，剩下的下一遍再說。

問題是「下一遍」怎麼知道還有剩。第一次試跑 `0x10d9d`（67 byte，function 的 entry 在 `0x10da0`，前面有 3 byte 填充）落地之後，重新匯出的清單裡**還是有一個 `0x10d9d`**，只是變成 3 byte。而「已經有判定檔就跳過」這條規則會把它當成做完的，於是那 3 個 byte 永遠不會被處理。

修法是比對判定檔記的 `end` 與現況的 `end`：不一致就把舊判定檔改名成 `.superseded-<舊end>`，該區塊重新回到清單。舊檔留著不刪，因為它產生的 entry 已經在 Ghidra 裡了。這一輪總共有 21 個判定檔被這樣退休。

## function body 上的洞

第七輪落地時 apply agent 回報 25 個一模一樣的問題：`DEAD CODE OUTSIDE ANY FUNCTION`。

那些區塊都是 3 個 byte 的 `83 c4 04`（`add esp,4`），位置都在呼叫 `crt_exit` 的正後方。判定 agent 全部判對了：這是 cdecl 的堆疊清理，編譯器照發，但 `crt_exit` 被標成不返回，Ghidra 就在呼叫處停止追蹤流程，那三個 byte 因此掉在所屬 function 的 body 之外。它們不是 function 的 entry，是 body 上的一個洞。

第一版的落地腳本用 `getFunctionContaining` 判斷「這段死碼屬於誰」，回傳 null 就拒絕動作。這正是問題本身：`getFunctionContaining` 問的是 body 這個位址集合，而 body 就是有洞的那個東西。改成用「上一個 function 的 body 尾端 ≥ 這段的結尾」來認領。

改完之後還是漏了兩個：`0x10001` 與 `0x44b5a`。它們不是洞，是**緊接在 body 尾端後面的尾巴**——`0x10000` 的 trap stub 是 `cc eb fd`，Ghidra 在 `INT3` 就終止流程，function body 只有 1 個 byte，後面的 `jmp` 自然不在範圍內；`0x44b5a` 是 16-bit 遠端返回 `66 cb` 之後的實模式收尾。條件放寬成「洞，或緊接在 body 之後」才收乾淨。

同一批還有一個 `DATA FAILED`：`0x54f3f` 是內嵌的 6 個 ASCII byte `WVIDEO`（Watcom 除錯主控台的 `INT3` 協定，前面一個 `eb 06` 跳過它），判定檔寫的型別是 `string`，Ghidra 的終止字串型別一路吃到 `0x54f45` 的指令上去。判定本身沒問題——那份判定的 note 就寫著「6 ASCII bytes, not NUL-terminated」——是型別選錯，改成 `byte[6]`，並在判定檔裡留下 `_transcription_note` 說明改了什麼、為什麼判定沒變。

## 撞到 1000 個 agent 的上限

跑到回掃段的一半時 workflow 以 `WorkflowAgentCapError` 收掉：單支 workflow 最多 1000 個 `agent()` 呼叫。用掉的是 800 個區塊判定、23 輪落地、幾次 refresh，再加上 161 個回掃。

沒有資料遺失——判定都在檔案裡，落地也都做完了。收尾用 `ApplyBlockTriage ... all` 重跑一次把前面幾輪還沒支援 dead code 時漏掉的 43 段補上，然後手動處理剩下的三個。但這說明一件事：這張票的規模（區塊 738 + function 1,347）本來就塞不進一支 workflow，得分成幾次呼叫。腳本靠判定檔續跑，所以分段是預算切的，不是決策切的。

## 收尾數字

- 區塊判定 800 份：填充 578、程式碼 125、資料 77、混合 20
- 新建 function 305 個（1,042 → 1,347）
- `.object1` 未定義 byte：43,143 → **0**
- 孤立程式碼 0、error bookmark 0，全程 23 輪落地每一輪都乾淨
- 仍有 45 份判定信心低或留有未決問題，多半是「這段程式碼屬於哪個程式庫」——那要等 pool 判定那一段才有材料回答
- 1,000 個 agent、42.1M token、4 小時 20 分，全程無人介入

`workspace/` 下的 agent 回報整理成 `devlog/runs/2026-08-15-pool-triage-blocks.json`。
