# data_emit — 全域資料的定義落地（票 23）

把連結器報為未定義的每個遊戲全域，以原版映像的初始內容寫成 `src/` 裡的 C 定義。定義怎麼決定、佈局約束、閘門判什麼、界線與足跡，全部由 [`rebuild_info/data_emit.md`](../../rebuild_info/data_emit.md) 擁有；本檔只講腳本與跑法。

前作 FD2 的對應工具是 `fd2-anatomy/tools/data_emit/`，沿用了它「連結器的未定義清單是權威缺口」與「與原版逐 byte 比對才算數」兩個結論；比對改成讀連結後的映像而不是解析 C 初值（FD2 的 `verify_real.py` 只懂平的 byte 陣列，指標表與 struct 要人工對），落地改成腳本而不是 agent。

## 腳本

| 檔案 | 用途 |
| --- | --- |
| `emit_ticket23.js` | Workflow。收拾 → 連結取清單 → 每個全域一個判定 agent（平行）→ 驗證判定檔、失敗重試一次 → 回掃低信心判定 → 擁有權變更 → 整批落地、build gate、修復或逐檔重試 → 再連結直到清單為空 → Ghidra 修正 → 知識庫 → devlog。帶 `rejudge` 時第一輪改判那些已落地的全域。不看自己的預算 |
| `worklist.py` | 工作清單：（`--build` 時先建置）讀 `workspace/code_emit/undefined.json`，每個未定義全域附上 routing 列、`.h` 裡現有的 `extern`、判定檔狀態、票 22 交接過來且點名它的疑慮編號。`--symbols a,b` 改列指定的全域（重判已落地的） |
| `land.py` | 轉錄段，不做判斷。`validate` 驗判定檔、`plan` 列出就緒的判定（已落地而判定內容改了的列在 `relanding`）、`apply --target X.c` 把就緒判定寫進該檔的資料區塊並更新 `manifest.json`、`remove --target X.c --addr A` 在擁有權搬家時拿掉舊檔的條目、`rescan-list`／`ghidra-fixes`／`summary` 給 workflow 的其他段用。`--selftest` |
| `check_data.py` | 閘門：每個已落地全域的連結後 byte 對原版 `FDPS.LE`，指標比指向的符號名，外加佈局約束。已排進 build gate 的 `emittest` 目標（套件 `data_emit.check`）。`--selftest` 以原版自己驗證它會說是也會說不 |
| `layout_probe.py` | 量測：Watcom 10.0a 與 wlink 怎麼擺帶初值與不帶初值的全域。結論在 `rebuild_info/data_emit.md` |

## 資料

| 檔案 | 內容 |
| --- | --- |
| `data/manifest.json` | 每個已落地全域一筆：原版位址、大小、目標檔、零值或帶初值、佈局約束、信心、判定依據一句、處理了哪些交接疑慮。`land.py` 寫、`check_data.py` 讀。進版控 |

判定檔在 `workspace/data_emit/verdicts/<符號>.json`，修復段擱置的判定在 `workspace/data_emit/held/`，落地中的足跡是 `workspace/data_emit/in_flight.json`，閘門結果是 `workspace/data_emit/check.json`。

## 跑法

```
python tools/data_emit/worklist.py --build        # 還剩哪些未定義
python tools/data_emit/land.py plan               # 哪些判定就緒
python tools/data_emit/land.py summary            # 已落地的精簡索引：分類、目標檔、佈局約束、交接疑慮
python tools/data_emit/check_data.py              # 已落地的與原版比對（先建置）
python tools/data_emit/layout_probe.py            # 重量工具鏈的全域擺放規則
python tools/build_gate/gate.py check --target emittest   # 正式閘門，含上一行
```

```
Workflow({ scriptPath: "tools/data_emit/emit_ticket23.js",
           args: { label: "t23-01", date: "YYYY-MM-DD" } })
```

`limit` 限制這一輪判定幾個全域（預設全部）。`rejudge`（符號名陣列）與 `rejudgeNote`（新證據的說明）讓已落地的全域帶著新證據逐一重判，判定改了的才重新落地。中斷後再呼叫一次即可：判定檔還在的不重判，開跑第一段會丟掉上一輪落地到一半的殘骸並回報它。
