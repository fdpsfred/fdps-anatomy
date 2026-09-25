# tools/cut_traces — 新痕跡逐條判定

票 25.9 的收尾報告列了 104 條「新痕跡」：驗證某條發現時順手看到、只記一句話而刻意沒判定的東西。任何一條要進 [`cut_content/`](../../cut_content/_index.md) 之前，都要單獨驗證並決定它落在哪裡。本資料夾把這件事做成一條一個 agent 的 workflow；票 25.10 的 23 條用它跑，`--ticket` 讓 25.11–25.14 也能拿來判自己的那一份。

前作沒有對應成果。形狀照 [`cut_verify/`](../cut_verify/_index.md)（固定清單、判定寫檔、以判定檔推導待辦與回掃清單、收尾報告進 `devlog/runs/`），但判定的內容不同：除了真假，還要決定**落點**。

## 檔案

| 檔案 | 用途 |
| --- | --- |
| `cuttrace.py` | 不做判斷的部分：`show` 印出單一條痕跡（judge agent 看到痕跡的唯一途徑）、`pending` 列出每條的狀態、`check` 是判定檔的 gate、`rescan` 列出需要第二位 agent 重讀的條目、`report` 產出收尾報告與判定彙整到 `devlog/runs/` |
| `traces_ticket25_10.js` | 票 25.10 的 workflow：一條痕跡一個 agent，每輪跑 gate、失敗重試一次、回掃、收尾報告 |
| `test_cuttrace.py` | 單元測試：`python -m unittest tools/cut_traces/test_cuttrace.py` |

## 跑法

```
Workflow({ scriptPath: "tools/cut_traces/traces_ticket25_10.js", args: { date: "YYYY-MM-DD" } })
```

中斷後原樣再跑一次即可續跑：已有判定檔且通過 gate 的條目不重判；已經有第二次閱讀記號（`_supersedes` 或 `_reread`）的判定不再回掃。

## 編號與判定檔

- 痕跡從 `devlog/runs/2026-09-25-cut-verify-summary.json` 的 `new_traces` 讀，依票過濾後照報告裡的順序編成 `T<票的小號>-NN`（25.10 是 `T10-01`…`T10-23`）。判定檔記下痕跡文字的 SHA-1，文字不同就視為過期。那份報告已歸檔，不會再變。
- 判定檔在 `workspace/cut_traces/<票>/judgements/<ID>.json`（可重生的中間產物）。收尾報告與判定彙整是 `devlog/runs/<date>-cut-traces-<票>-summary.json` 與 `-judgements.json`；執行被中止時不寫彙整。

## 落點（disposition）

| 值 | 意思 | 必填 |
| --- | --- | --- |
| `absorbed` | 事實已經寫在 `cut_content/` 的某個條目或排除清單裡 | `entry` |
| `addendum` | 屬於某個既有條目、條目還沒寫，值得補 | `entry`、`kb_text` |
| `new_entry` | 本票主題檔的一個新條目 | `topic`（本票的主題）、`category`（五類之一）、`title`、`kb_text` |
| `exclude` | 屬排除，進 `_index.md` 的排除清單 | `topic`、`category = excluded`、`title`、`kb_text`（理由） |
| `route` | 不屬本票的主題檔：原版 bug 的機制（`known_bugs`）、只是重建踩雷點（`pitfalls`）、知識庫／`src/` 註解／Ghidra 的錯誤（`kb_fix`，交 25.15）、別的主題（`25.11`–`25.14`） | `route`、`route_note` |
| `drop` | 不成立，或成立但沒有值得記的東西；不成立的一律 `drop` | — |

踩雷點另記在 `pitfall_candidate`，與落點無關。gate 另外要求至少一條一手證據（`src` 的 `檔名:行號`、`ghidra` 位址、`data` 的 `檔名@偏移`）。

**落地不在 workflow 裡。** judge agent 對 `src/`、Ghidra、知識庫一律唯讀；`kb_text` 由票自己的 session 從判定彙整轉錄進 `cut_content/`，轉錄後跑 `python tools/cut_content/cut_content.py check` 當落地閘門。凡是會落地的判定（`addendum`、`new_entry`、`exclude`）、判定不是「成立」的、信心不是 high 的、留有待決問題的，都會被回掃段交給第二位 agent 重讀。
