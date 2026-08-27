# global_data — 全域資料命名與 struct 佈局

把 `FDPS.LE` 的每個全域資料符號逐一讀過所有取用它的指令之後給它語意名稱與真正的型別，再以這批判定為輸入把主要的 struct 佈局定出來。這是票 17 專屬的 workflow，不是給別票用的框架（[ADR-0007](../../docs/adr/0007-workflow-automation-and-agent-context.md)）。

| 檔案 | 用途 |
| --- | --- |
| `globals_ticket17.js` | 主 workflow：判定 → 落地 → 重新匯出證據 → 兩道 gate → 撞名與邊界仲裁 → 回掃 → struct 階段 → 收尾報告，全程無人介入 |
| `DumpGlobalState.java` | 匯出每個 anchor 的位元組、每一條取用它的指令與所在函式、反編譯中提到它的行，唯讀 |
| `build_worklist.py` | 算出還沒判定的清單，並退休已經不符現況的判定檔 |
| `build_vocabulary.py` | 產生命名語彙頁：本程式已定案的名稱＋前作 FD2 的全域名稱與已解出的 struct 佈局 |
| `collect_structs.py` | 從判定檔的 `struct_candidate` 欄位收出 struct 清單，併入票本身指名的那幾個，算出名稱相近的候選供 agent 判斷是否同一筆記錄，並把判定為 `alias_of` 的折疊掉 |
| `ApplyGlobalVerdicts.java` | 把名稱、型別與註解轉錄進 Ghidra |
| `ApplyStructDefs.java` | 把 struct 佈局建進型別管理員，並套用到判定指名的全域與區域變數 |
| `AuditGlobals.java` | 本票專屬的 gate：預設命名殘留、前綴、名稱裡的位址、撞名、命名了卻沒型別、跨段邊界、孤立標籤 |

## 執行

```bash
python tools/global_data/build_worklist.py
```

```
Workflow({ scriptPath: "tools/global_data/globals_ticket17.js",
           args: { roundSize: 16, maxItems: 128 } })
```

不給 `addrs` 時 workflow 自己開一個 agent 跑 `build_worklist.py` 並讀回清單——腳本本身讀不到檔案系統，這是唯一的取得途徑。**可重跑**：判定檔存在且與現況相符的 anchor 不會再判一次，所以每次呼叫都從上次停下的地方接。1,100 多個 anchor 塞不進一次 session，分次跑是常態。

struct 階段只在 anchor 全部判完之後才跑，因為 struct 清單是從判定檔的 `struct_candidate` 欄位長出來的。要單獨跑它用 `args: { structs: true }`。

## anchor 不等於變數，這是本票的核心難處

`DumpGlobalState.java` 匯出的單位叫 **anchor**——某條指令當成資料讀寫的位址，加上寫在可寫區塊裡的既有 defined data。它是「參照落在哪裡」的事實，**不是「變數從哪裡開始」的主張**。程式碼一天到晚參照陣列與結構的中間。

所以判定的第一個問題不是「這叫什麼」而是「這是不是一個變數」。判定檔的 `classification` 可以是 `interior`，附上 `belongs_to`；落地腳本遇到 interior 就完全不改名，只留一則 EOL 註解說它屬於誰——那些位元組的名字要由擁有者的型別給，當成欄位或陣列元素。

由此衍生出兩種本票獨有的爭議，兩種都會被落地腳本原樣回報再交給仲裁 agent：

- **撞名**：兩份判定搶同一個符號。
- **邊界**（`OVERLAP`）：一份判定的型別大到蓋過另一份判定宣稱自己是變數的位址。落地腳本**不會**替它決定誰對，型別不套用、原樣回報。仲裁 agent 讀兩邊的定址方式：`base + index * stride` 落在第二個位址上就是大物件對，第二個位址是絕對立即數載入且與第一個沒有算術關係就是大物件的 size 錯了。

輸掉邊界爭議的 anchor 會被 `build_worklist.py` 放回清單（判定檔說它是變數、但現在它落在別人的物件裡），重判一次就會看出自己是 interior。

## 字串字面值不在清單上

415 個字串已經帶著 Ghidra 的 `s_` 標籤，而重建之後它們是敘述句裡的字面值而不是具名全域，替它們取名等於憑空造出原版沒有的符號。理由與例外（指向它們的指標表是實實在在的具名資料）記在 [`rebuild_info/naming.md`](../../rebuild_info/naming.md)。它們仍然留在匯出裡，因為判定別的 anchor 時會需要看到它們。

## gate 有兩道

基準稽核（`tools/ghidra_baseline/AuditGhidraBaseline.java`）：孤立程式碼 0、error bookmark 0、`.object1` 未定義 byte 0。

全域稽核（`AuditGlobals.java`）分兩種計數，這是它能在票跑到一半時執行的原因：

- **pending** — 還掛著預設或載入器產生的標籤，也就是還沒有人判定過。票跑完時歸零。
- **violation** — 已落地的判定弄錯的東西：前綴不合 [`naming.md`](../../rebuild_info/naming.md)、名稱裡有位址、兩個 anchor 搶同一個符號、命名了卻還是 `undefined` 型別、變數跨過 `_edata` 或 `_end`、標籤卡在別的物件中間。**任何時候都不接受**，當輪修不掉就停。

它另外每輪重新量一次初始化資料的結尾，那是本票對「資料段與未初始化段的分界」這個問題的答案：`.object2` 裡最後一個非零的初始化 byte 落在 `0x6392a`，在 `_edata`（`0x63930`）之下，與 [`program_info/memory_layout.md`](../../program_info/memory_layout.md) 一致。

## 錯誤處理

[ADR-0007](../../docs/adr/0007-workflow-automation-and-agent-context.md) 第五條的實作對照：

| 狀況 | 行為 |
| --- | --- |
| 單一 agent 沒回傳或沒寫判定檔 | 重試一次，仍失敗記進未完成清單並繼續 |
| 一輪裡的 agent 全部沒回傳 | 判定為上游失效，立刻停止並列出未動過的項目，不送重試 |
| 兩份判定搶同一個名字 | 落地腳本原樣保留並回報，仲裁 agent 改輸家的判定檔，重跑落地 |
| 兩份判定對邊界不一致 | 型別不套用、原樣回報，仲裁 agent 改錯的那一份，重跑落地 |
| 判定回報與記憶體佈局矛盾 | 只回報不處理——`segment_check` 進未完成清單交給人看 |
| gate 不過 | 該輪修復，修不掉停止 |
| Ghidra 沒回應 | 落地 agent 回報 `ghidra_responding: false`，立刻停止 |
| 已停止 | 不寫知識庫、不產下游清單，但仍輸出收尾報告與未完成清單 |

## 已知的坑

**`region_sha` 不能把「到下一個 anchor 的距離」算進去。** 第一版這樣做，結果是套用型別——本票的全部目的——會把物件內部原本獨立的 anchor 吸收掉、拉長距離、改掉雜湊、退休剛剛才落地的那份判定，清單永遠清不完。現在只雜湊固定 64 byte 的視窗；「被鄰居吞掉」這件事改由 `inside_defined_data` 直接判斷。

**Ghidra 沒有叫 `code` 的型別。** 函式指標表寫 `code *[30]` 會整個解析失敗，anchor 留在無型別狀態。落地腳本認的拼法是 `func_ptr`，背後是 `/fdps/void_fn` 這個 `void (void)` 的 placeholder——每個 slot 真正的簽章是 emit 階段的問題，這一步只要它是程式碼指標，Ghidra 才會去追那些 slot。

**`binary_artifact_` 橫跨 pool，不與其他 pool 並列。** wcc386 把 auto 陣列的初值影像擺在宣告它的函式旁邊的唯讀資料裡，所以碰得到那些位元組的只有遊戲程式碼——但它仍然是編譯器產物，不是誰寫的變數。稽核照 pool 要求前綴時必須替 `binary_artifact_` 開豁免，否則會逼出一個宣稱原版有某個全域的名字。

**「名稱裡有位址」不能用「連續 hex 字元」判。** `decade` 六個字元全是 hex，`added` 五個也是。判準是逐一檢查底線分隔的字段，全 hex 且長度 4–8、而且解析出來的位址真的落在本程式的記憶體範圍內才算。

**Ghidra MCP 的 `rename_data` 會擋掉本專案的全域命名。** 它內建的 `g_` 前綴檢查在 `tools/ghidra_config/conventions.json` 裡已經關掉，但工具仍然拒絕。落地一律走 Ghidra script API（`createLabel` / `Symbol.setName`），不受它管——這也是落地必須是 Java 腳本而不是一連串 MCP 呼叫的原因之一。

**清掉一個 Data 的任一 byte 會清掉整個 Data。** 落地腳本原本逐個 target 做 `clearListing` + `createData`，三份佈局搶同一段時，最後寫的那個把前面 122-byte 的 `L$N_emu387_state` 整個抹掉，而腳本回報成功。現在所有 global target 在寫入前先做範圍比對，兩份佈局重疊就**兩邊都不套用**、原樣回報交給仲裁；要寫的位址若落在別人已定義的物件中間，同樣拒寫。

**struct 落地每輪都要套用全部，不能只套當輪新增的。** 會撞的兩份佈局通常隔了好幾輪寫成，只套當輪的名單，跨輪衝突永遠不會在 run 裡浮現，等發現時其中一個已經被摧毀了。重套已在資料庫裡的佈局零成本。

**`extra_anchors.txt` 要累積寫入全部，不能只寫「目前不是 anchor 的」。** 只寫缺的會讓檔案自己清空：擁有者被納入 → 下次 build_worklist 看到它已經是 anchor 就不寫 → 下次 dump 又丟掉它，來回震盪。六個擁有者裡有三個這樣擺盪過。

**判定改成 interior 時要撤掉舊名字。** 一個位址可能前一輪被判成變數而取了名，struct 落地後回掃把它改判成 interior——名字不撤就變成卡在別人物件裡的孤立標籤，稽核會擋。

**`declare` 與 `fill` 要分成兩趟。** linked list 的 `next` 指向自己這個型別，欄位放進去時該型別必須已經在型別管理員裡。同一輪裡互相引用的 struct 也靠這一趟解決。

**跨 struct 相依要當成 `DEFERRED` 而不是錯誤。** `fdps_save_slot` 裝著 `fdps_unit_record[31]`，兩者無論誰先判都會指名還不存在的對方。當成 gate 失敗會讓這對永遠解不開；當成「等對方落地就自己好」則會自動收斂，收尾時再跑一次落地重新量測還剩哪些。

**反編譯器顯示名不是資料庫裡的變數。** `local_30`、`auStack_50` 把堆疊位移編在名字裡，可以機械解出；`puVar1`、`iVar3` 是純顯示暫存，資料庫裡沒有那個東西，套不上型別是事實而不是錯誤。

**Java 三元運算子混 `int` 與 `Integer` 會強制拆箱。** `cond ? json.getAsInt() : maybeNull()` 在 `maybeNull()` 回 null 時直接 NPE。要寫成 if/else。

**workflow 腳本不能有 CR。** 用 Python 在 Windows 上寫 `.js` 時預設會把 `\n` 轉成 `\r\n`，Workflow 工具會以「script contains control characters」拒絕整份腳本。以 binary 模式寫，或寫完轉成 LF。（沿用票 15 的結論。）
