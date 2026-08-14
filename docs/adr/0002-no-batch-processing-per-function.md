# 禁止批次或抽樣決定 function 的身分、calling convention 與 emit

每個 function 的 pool 歸屬、calling convention 判定、以及 emit 成 C 的工作，都必須逐一親自讀過該 function 的 assembly 後才下判斷，一次處理一個。禁止用腳本或規則批次套用，也禁止抽樣後外推。

理由是批次處理的本質就是「用一個涵蓋不了所有情況的規則去換時間」，而例外永遠存在。事後逐一複查的成本比一開始就逐一判定更高，因為錯誤已經擴散到下游。

## Considered Options

曾經考慮「批次全部套用 `__watcall`，再挑出例外修正」。這個做法在本專案會直接出錯——證據顯示 `FDPS.LE` 連結的是 `CLIB3S.LIB`（stack-based calling convention），批次套用暫存器傳參的 `__watcall` 會讓全部 function 的簽章錯誤，與原本 compiler spec 掛成 `borlandcpp` 的問題同樣嚴重，只是換個方向錯。

也考慮過靠 prompt 要求 agent「一次只處理一個」。實測證明無效：agent 在處理一段時間後仍會自行退化成批次處理。

## Consequences

強制機制必須是**結構性**的，不能靠指示。做法是用 workflow 腳本驅動迴圈，工作清單只存在於腳本中，每次 agent 呼叫的 prompt 只帶一個 function——agent 拿不到第二個 function，因此不存在批次的可能。

平行化透過腳本的 `pipeline()` 達成，而不是把 function 清單交給單一 agent 自行分配。到了 emit 階段則改用序列模式，因為該階段的 reviewer 依賴 `git diff HEAD` 檢視未 commit 的改動，這要求一次只有一個 function 在飛。

代價是整體工時顯著拉長。這是刻意接受的取捨。
