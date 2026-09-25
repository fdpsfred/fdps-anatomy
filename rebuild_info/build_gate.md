# Build gate — 自我回歸閘

任何改動如果意外改變了編譯結果，跑一次閘門就會知道。閘門比對的是**本專案的前一版建置結果**，不是原版 `FDPS.EXE`——功能等價不是位元組相同（[ADR-0001](../docs/adr/0001-only-functional-equivalence.md)），拿原版當比對基準在這個專案裡沒有意義。閘門的用途是防止「本來不該改變輸出的改動」偷偷改變了輸出。

腳本在 [`tools/build_gate/`](../tools/build_gate/_index.md)。建置流程本身由 [`build_pipeline.md`](build_pipeline.md) 擁有，旗標組由 [`build_flags.md`](build_flags.md) 擁有，本檔只回答「閘門怎麼判、基準值什麼時候該動」。

## 通過的條件

一次 `check` 對每個建置目標跑完整建置，再逐項判定。任何一項不過，整個閘門就是 FAIL。

| 項目 | 判定 |
| --- | --- |
| build | 產出執行檔存在，且建置流程有完成標記（三訊號結束偵測，見 [`build_pipeline.md`](build_pipeline.md)） |
| errors | `Error!` 行 0 個，且每個 translation unit 的摘要行回報 0 errors |
| undefined | 連結器的未解析符號 0 個。`emittest` 連結兩次，這一項判的是**第二次**——第一次刻意不帶 stub，它報出來的是還沒 emit 的資料與 function，是清單不是錯誤（[`emit_pipeline.md`](emit_pipeline.md)） |
| warnings | 出現任何**基準值沒記錄過的警告**就不過。比對的是警告文字不是數量，換掉一個警告不會蒙混過關 |
| equivalence | 產出與基準值的關係落在下表的前四級 |

另外每個註冊的測試套件都要通過。因環境缺件而跳過的套件會逐一列在結果裡，不會靜默消失。

### 不做映像比對的目標

`emittest` 是唯一一個 `equivalence` 顯示 `not compared` 的目標。它把 `src/` 與 `tests/` 編成單元測試映像，內容**按設計**每 emit 一支 function 就變一次；替它記基準值只會每次都紅、每次都被推進，那是一個被訓練成永遠說 yes 的閘門。其餘四項照樣判，而且 `warnings` 在沒有基準值時取最嚴格的形式：零。要接受一個警告就照樣得用 `update --reason` 記錄它。

這個目標保證的是「編得過、連得起來、測試全綠」，不保證映像沒動——它本來就會動。它擋不到的東西由 [`emit_pipeline.md`](emit_pipeline.md) 的八類隱性契約檢查表在 emit 當下擋。

### 遊戲本體目標

`game` 是出貨的那個執行檔（`FDE.EXE`，[`build_pipeline.md`](build_pipeline.md)），它**有**映像基準值。理由與 `emittest` 相反：遊戲的原始碼不再按設計一支一支長大，它的映像任何變動不是刻意的修正就是回歸——實機驗證修掉一個偏差，就以 `update --reason` 寫明修了哪支 function、哪個現象。

它另外帶兩個只對它跑的套件：全域資料逐 byte 比對與 RLE 組語指令比對，對象換成遊戲映像（為什麼兩個映像都要驗，見 [`build_pipeline.md`](build_pipeline.md)）。

**修正之後要跑的是整個閘門（不帶 `--target`），不是只跑 `game`。** 單元測試套件 `code_emit.run` 綁在 `emittest` 目標上，只選 `game` 會把它報成範圍外而跳過；修正改的是 `src/`，兩個目標都受影響。

## 六種等價判定

雜湊相同是最強也最便宜的答案，先問它。雜湊不同時才問第二個問題：差異是不是只落在連結器自己會重寫、或編譯器從來不寫的地方。

| 判定 | 意思 | 過閘 |
| --- | --- | --- |
| `identical` | 逐 byte 相同 | ✓ |
| `strict` | 差異只是 LE Fixup Record Table 的順序重排（byte multiset 不變） | ✓ |
| `reloc` | 差異只有重定位值加上上述重排 | ✓ |
| `pad` | 另外還有差異，但全部落在證明得出來的對齊空隙裡，而且兩邊的空隙位置完全相同 | ✓ |
| `different` | 有 code 或 data 的 byte 落在所有重定位位置與對齊空隙之外而改變了，或空隙的位置本身變了 | ✗ |
| `size` | 大小就不同，更細的比對沒有意義 | ✗ |

`strict` 與 `reloc` 存在的理由是兩個「binary 看得見但行為中性」的效果，兩者都由前作 FD2 在數百個 object 的連結上實測確立，wlink 與 LE 容器都相同，因此原封沿用：

1. **Fixup 重排。** wlink 送出 LE fixup record 的順序與符號名有關，改名會讓那張表的 byte 重排，但重定位的集合不變。
2. **Tentative definition 移位。** 未初始化的全域是 Watcom 的 COMDEF，wlink 依名稱排序擺放；改一個名可能讓它與鄰居位移幾個 byte，於是每個指向它的 fixup **site 值**與對應 record 的 target 欄位跟著變。載入後的映像行為完全相同。

判定的做法是把這兩種效果會動到的 byte 全部抹零再比殘差：整張 Fixup Record Table，加上每個 fixup site 的 1／2／4／6 byte 值（寬度依 LE 的 source type 決定）。表界與每個 site 都從 header 即時解析，沒有硬編。真正改到 code 或 data 的改動必定落在這些位置之外，殘差就會破掉。

### 第三個效果：`wcc386` 不清零的對齊空隙

`wcc386` 把資料對齊到邊界時只移動位置、不寫入跳過的 byte，那些 byte 裝的是編譯器緩衝區的殘值，跟著被編譯的文字變。實測觸發它的是**標頭檔的換行字元**：同一份 `src/` 把全部 `.h` 從 git 取出的 CRLF 改成 agent 直接寫檔的 LF，映像差 305 byte（只改 `fdpstype.h` 一個檔就差 6 byte）；把全部 `.c` 改成 LF、在 `.c` 或 `.h` 加改註解、改巨集名，映像都逐 byte 不變。所以主工作目錄（混有 LF 檔）與乾淨的 checkout 建出來的映像不同。沒有任何程式讀這些 byte，但它們落在所有重定位位置之外，只看重定位的比對會把它們報成 `different`。這個效果只出現在資料段，`_TEXT` 沒有任何 byte 因此不同。

`pad` 這一級把這些 byte 抹零再比。空隙由 [`tools/build_gate/lepad.py`](../tools/build_gate/_index.md) 從**該次建置自己留下的東西**算出——連結器 map、編譯出的 OMF 目的檔、被編譯的那份原始碼（建置時暫存的副本）——而且只抹證明得出來的兩種：

| 空隙 | 在哪 | 怎麼證明 |
| --- | --- | --- |
| 字面值空隙 | `CONST` | 程式用到的每個字面值都是某個重定位的目標，起點因此已知。起點要在 4 byte 邊界上，而且**每一個**指向它的重定位都把它的位址當值用：位在資料物件裡（一個存起來的指標），或位在 code 裡、前一個 byte 是 `push imm32`（`68`）或 `mov r32,imm32`（`B8`–`BF`）的 opcode。空隙是第一個 NUL 之後到下一個 4 byte 邊界，而且下一個被引用的東西或符號**正好**從那個邊界開始 |
| 符號空隙 | `_DATA`、`CONST2` | 符號起點取自 map（public）與目的檔的 `LPUBDEF`（檔案範圍的 `static`，map 不列；以同模組 public 的位址換算），每個重定位目標也算起點。符號大小取自被編譯的原始碼裡它唯一的檔案範圍定義，只認純量、指標與整數字面值維度的陣列。空隙是符號結尾到下一個起點，而且那個起點**正好**是結尾之後第一個 2 或 4 byte 邊界 |

`CONST` 不只裝字串：`wcc386` 把浮點常數也放在這裡（例如 `1.5`、`1.15` 的 `double`），它們經 x87 記憶體運算元讀取（`DC 0D disp32` 之類），前一個 byte 是 ModRM 而不是上面兩種 opcode，所以不會被當成字面值。只看「NUL 之後補到 4 byte」會把某些 `double` 的指數 byte 當成空隙抹掉。

證明不了的一律照常比對，不猜：宣告大小取不到的符號（struct、typedef、巨集維度）之後的 byte、沒辦法證明是字串的 `CONST` 項目（例如被 `mov ax,[m]` 整個讀進暫存器的短字面值）、找不到同模組 public 而無法定位的 `static`。空隙裡若有重定位位置也不算空隙。空隙的位置本身記進指紋，所以宣告變大或變小使空隙移動時，判定是 `different` 而不是 `pad`。

做這一級需要 map，所以只有 `game` 與 `ailsmoke` 有它；`smoke` 不編 `src/`、沒有 map，`emittest` 不比映像。兩份建置到底差在哪些 byte、各屬哪一類，用 [`tools/build_gate/pad_diff.py`](../tools/build_gate/_index.md) 看：它用與閘門相同的規則把每個不同的 byte 分成重定位、空隙、無法解釋三類，無法解釋的列出前後的 map 符號。閘門報 `different` 時先用它找出是哪個 byte。

## 基準值的更新

基準值存在 `tools/build_gate/data/baselines.json`，一個建置目標一筆，進版控——放在 `workspace/` 下就會跟著中間產物一起消失。每筆記的是日期、當時的 commit、**更新的理由**、可接受的警告文字，以及重定位感知的指紋；有 map 的目標，指紋另帶空隙的位置與抹掉空隙後的殘差。舊的一筆推進到 `history`，鏈條不刪，因為那就是「閘門被要求接受過什麼」的完整記錄。

基準值只存指紋、不存映像，所以 `pad` 要兩邊都帶空隙指紋才判得出來；沒有空隙指紋的基準值遇到只差空隙的建置，照樣是 `different`。有了 `pad` 之後，同一個 commit 在任何工作目錄狀態（主工作目錄、乾淨的 git checkout、worktree）建出來的 `game` 與 `ailsmoke` 都判為過閘的等級，基準值不必因為換了地方建置而推進。

推進的指令要求寫理由，沒有理由不給推進：

```
python tools/build_gate/gate.py update --target smoke --reason "為什麼輸出該改變"
```

它會先重建一次，建置有錯誤或未解析符號就拒絕記錄——基準值只能從乾淨的建置取得。建置帶警告時仍可記錄，但警告會逐條印出來，讓理由必須交代它們。

### 什麼情況該推進，什麼情況是回歸

判準只有一個：**輸出的改變是不是這次改動本來就要造成的。**

| 情況 | 處置 |
| --- | --- |
| emit 了新的 function 或資料、修了行為、換了旗標 | 推進，理由寫清楚改了什麼 |
| 只改註解、只改文件、只改 `tools/`、只改換行字元、只改值不變的巨集名 | 允許 `identical`／`strict`／`reloc`／`pad`，不必推進。報 `different` 就是有東西被連帶改到：用 `pad_diff.py` 找出是哪個 byte，照回歸處理，不要推進 |
| 閘門本身換了指紋的組成（例如新增空隙指紋） | 映像沒變也要推進才用得到新的判定；理由寫明指紋改了什麼，並附上推進前的判定（應為 `identical`） |
| 只改名（符號、參數、檔名） | 允許 `identical`／`strict`／`reloc`，不必推進。若報 `different`，那就不是純改名 |
| 閘門報 `different` 而你不知道為什麼 | 一律當回歸。先解釋清楚再決定，推進基準值是把問題永久蓋掉 |
| 改了某個目標也會編到的共用原始碼 | 那個目標的基準值也要推進。`ailsmoke` 會編 `src/dpmi.c` 與 `src/ailflags.asm`，改它們卻只跑 `--target emittest`，`ailsmoke` 就會一直紅著沒人發現；所以收尾要跑一次不帶 `--target` 的完整閘門 |

## 呼叫方式

```
python tools/build_gate/gate.py                     # 全部目標，建置 + 比對 + 測試
python tools/build_gate/gate.py check --target emittest   # emit 的閘門
python tools/build_gate/gate.py check --target game       # 只建遊戲本體（快速迴圈；修正後仍要跑全部）
python tools/build_gate/gate.py check --target smoke --skip-tests
python tools/build_gate/gate.py check --json        # 結構化結果印到 stdout
python tools/build_gate/gate.py check --with-audio  # 連音效實跑套件一起
python tools/build_gate/gate.py show                # 目前的基準值與推進鏈
python tools/build_gate/gate.py selftest            # 驗證閘門自己的每一項判定
```

建置腳本要在前景呼叫時直接 `import gate` 用 `gate.check(...)`，它回傳的 dict 與 `--json` 印出來的是同一份，也一律落到 `workspace/build_gate/result.json`。離開碼 0 只在整個閘門通過時給。

## 閘門看不到的東西

**重定位感知的比對看不見「某個 fixup 改指到別的符號」。** 這種改動只動到 site 的位移值與該筆 record 的 target 欄位，而這兩處在比對前都被抹零了，於是判定會是 `reloc`——語意已經變了，閘門卻說過。前作就是在這裡被咬過一次（把價格讀成了鄰居的表）。收錄在 [`pitfalls.md`](pitfalls.md)，處理方式也在那裡。

換句話說：閘門通過只保證「沒有新增的 code／data 位元組差異」，不保證「每個 fixup 還指向原本的符號」。

**`pad` 看不見字面值中間的 NUL 之後、同一個 4 byte 區段內的字元。** 字面值的長度是從第一個 NUL 算的；`"a\0b"` 這種字面值，`b` 與結尾的 NUL 會落在被抹掉的空隙裡，改掉 `b` 判定仍是 `pad`。`src/` 目前沒有任何字面值中間帶 NUL，寫新的字面值時不要這樣寫（收錄在 [`pitfalls.md`](pitfalls.md)）。

`selftest` 要一起跑。閘門的每一項判定都必須被證明會失敗過——一個永遠不會 FAIL 的閘門在壞掉的建置上也會說 PASS。`gate.py selftest` 用一份自己合成的 LE 映像逐項驗證：解析器找出的重定位位置與預先安排的位置逐一相符、記錄流被打亂時會報錯而不是回一個看似合理的錯答案，以及各判定由「該判定所描述的那種改動」實際產生一次。`lepad.py selftest`（測試套件 `build_gate.pad_selftest`）另用一份合成的建置——LE 映像、map、目的檔與原始碼——在已知位置擺好字面值、`double`、`static`、不知大小的 struct，驗證空隙恰好是預先安排的那幾段，並且「只動空隙」判 `pad`，而改字串、縮短字串、改常數、改一個 code byte、改 `double` 的指數、改不知大小的符號之後的 byte、把宣告改寬，都判 `different`。
