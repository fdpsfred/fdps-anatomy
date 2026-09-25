# tools/cut_verify — 刪減發現逐條驗證

票 25.9 專屬。刪減與未用的調查留下 53 條發現，每條都只是待驗證的主張；本資料夾把每一條交給一個 agent 從 `src/`、Ghidra 與資料檔獨立驗證，得到「成立／不成立／需修正」與分類，讓 25.10–25.14 只轉錄通過的結論。分類詞彙見 [`CONTEXT.md`](../../CONTEXT.md)「刪減與未用」。

前作沒有對應成果：`fd2-anatomy/tools/` 的 `kb_overhaul` 是章節表產生器、`rsrc_unresolved` 是資源死碼的一次性驗證，都不是逐條判定的 workflow。形狀參照的是本專案 [`crt_version/`](../crt_version/_index.md)（固定清單、判定寫檔、以判定檔推導待辦與回掃清單）。

## 檔案

| 檔案 | 用途 |
| --- | --- |
| `cutverify.py` | 不做判斷的部分全在這裡：`show` 印出單一條發現（judge agent 看到主張的唯一途徑）、`pending` 列出每條的狀態、`check` 是判定檔的 gate、`rescan` 列出需要第二位 agent 重判的條目、`report` 產出收尾報告與判定彙整到 `devlog/runs/` |
| `verify_ticket25_9.js` | workflow：一條發現一個 agent，每輪跑 gate、失敗重試一次、回掃、收尾報告、devlog 與 `pitfalls.md` |
| `test_cutverify.py` | `cutverify.py` 的單元測試（`python -m unittest tools/cut_verify/test_cutverify.py`） |

## 跑法

```
Workflow({ scriptPath: "tools/cut_verify/verify_ticket25_9.js", args: { date: "YYYY-MM-DD" } })
```

中斷後原樣再跑一次即可續跑：已有判定檔、通過 gate、且與目前主張文字一致的條目不重判；已經有第二次閱讀記號（`_supersedes` 或 `_reread`）的判定不再回掃。

## 產物

| 位置 | 內容 |
| --- | --- |
| `workspace/cut_verify/verdicts/<ID>.json` | 每條一份判定檔（可重生） |
| `devlog/runs/<date>-cut-verify-summary.json` | 收尾報告：完成數、未完成清單、依判定／分類／目標子票的清單、分類變動、回掃結果、留待確認的條目、新發現的痕跡、`pitfalls.md` 候選 |
| `devlog/runs/<date>-cut-verify-verdicts.json` | 全部通過 gate 的判定原文，**25.10–25.14 從這裡讀驗證後的發現**。執行被中止時不產生，免得半套資料看起來像完整的 |

## 判定檔的約定

- **主張不複製，從票檔讀。** `cutverify.py` 直接解析票檔的「發現清單」一節，只切結構（粗體編號開頭的頂層條目、所屬 `###` 節標出的目標子票），不解讀內文。判定檔記下所判主張的 SHA-1；票檔的主張措辭一改，舊判定就在 gate 上變成 stale 並回到待辦。**所以收尾之後不要改發現清單的文字。**
- **一手證據才算數。** gate 要求至少一條 `src`（`檔名:行號`）、`ghidra`（位址）或 `data`（`檔名@偏移`）證據；調查自己的產出、攻略站與前作只能佐證。這是票面「不能以調查的輸出代替親自讀 C code 與資料」的結構性落實。
- **分類另設 `negative`（否定性結論）。** 調查清單裡的「沒有除錯鍵」「全部有 xref」這類主張斷言某物不存在，沒有痕跡可分類，不塞進五類中的任何一類。
- **分類跟建議不同不等於需修正。** 清單的分類本來就是建議；改判分類只會觸發回掃，不會讓判定變成 `needs_correction`。
- **判定的文字欄位用繁體中文**，因為下游直接轉錄成知識庫；列舉值與欄位名用英文。
