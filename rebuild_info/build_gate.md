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
| equivalence | 產出與基準值的關係落在下表的前三級 |

另外每個註冊的測試套件都要通過。因環境缺件而跳過的套件會逐一列在結果裡，不會靜默消失。

### 不做映像比對的目標

`emittest` 是唯一一個 `equivalence` 顯示 `not compared` 的目標。它把 `src/` 與 `tests/` 編成單元測試映像，內容**按設計**每 emit 一支 function 就變一次；替它記基準值只會每次都紅、每次都被推進，那是一個被訓練成永遠說 yes 的閘門。其餘四項照樣判，而且 `warnings` 在沒有基準值時取最嚴格的形式：零。要接受一個警告就照樣得用 `update --reason` 記錄它。

這個目標保證的是「編得過、連得起來、測試全綠」，不保證映像沒動——它本來就會動。它擋不到的東西由 [`emit_pipeline.md`](emit_pipeline.md) 的八類隱性契約檢查表在 emit 當下擋。

## 五種等價判定

雜湊相同是最強也最便宜的答案，先問它。雜湊不同時才問第二個問題：差異是不是只落在連結器自己會重寫的地方。

| 判定 | 意思 | 過閘 |
| --- | --- | --- |
| `identical` | 逐 byte 相同 | ✓ |
| `strict` | 差異只是 LE Fixup Record Table 的順序重排（byte multiset 不變） | ✓ |
| `reloc` | 差異只有重定位值加上上述重排 | ✓ |
| `different` | 有 code 或 data 的 byte 落在所有重定位位置之外而改變了 | ✗ |
| `size` | 大小就不同，更細的比對沒有意義 | ✗ |

`strict` 與 `reloc` 存在的理由是兩個「binary 看得見但行為中性」的效果，兩者都由前作 FD2 在數百個 object 的連結上實測確立，wlink 與 LE 容器都相同，因此原封沿用：

1. **Fixup 重排。** wlink 送出 LE fixup record 的順序與符號名有關，改名會讓那張表的 byte 重排，但重定位的集合不變。
2. **Tentative definition 移位。** 未初始化的全域是 Watcom 的 COMDEF，wlink 依名稱排序擺放；改一個名可能讓它與鄰居位移幾個 byte，於是每個指向它的 fixup **site 值**與對應 record 的 target 欄位跟著變。載入後的映像行為完全相同。

判定的做法是把這兩種效果會動到的 byte 全部抹零再比殘差：整張 Fixup Record Table，加上每個 fixup site 的 1／2／4／6 byte 值（寬度依 LE 的 source type 決定）。表界與每個 site 都從 header 即時解析，沒有硬編。真正改到 code 或 data 的改動必定落在這些位置之外，殘差就會破掉。

## 基準值的更新

基準值存在 `tools/build_gate/data/baselines.json`，一個建置目標一筆，進版控——放在 `workspace/` 下就會跟著中間產物一起消失。每筆記的是日期、當時的 commit、**更新的理由**、可接受的警告文字，以及重定位感知的指紋。舊的一筆推進到 `history`，鏈條不刪，因為那就是「閘門被要求接受過什麼」的完整記錄。

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
| 只改註解、只改文件、只改 `tools/` | 不該有任何改變。閘門若報 `different` 就是有東西被連帶改到，去找它，不要推進 |
| 只改名（符號、參數、檔名） | 允許 `identical`／`strict`／`reloc`，不必推進。若報 `different`，那就不是純改名 |
| 閘門報 `different` 而你不知道為什麼 | 一律當回歸。先解釋清楚再決定，推進基準值是把問題永久蓋掉 |

## 呼叫方式

```
python tools/build_gate/gate.py                     # 全部目標，建置 + 比對 + 測試
python tools/build_gate/gate.py check --target emittest   # emit 的閘門
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

`selftest` 要一起跑。閘門的每一項判定都必須被證明會失敗過——一個永遠不會 FAIL 的閘門在壞掉的建置上也會說 PASS。它用一份自己合成的 LE 映像逐項驗證：解析器找出的重定位位置與預先安排的位置逐一相符、記錄流被打亂時會報錯而不是回一個看似合理的錯答案，以及五種判定各由「該判定所描述的那種改動」實際產生一次。
