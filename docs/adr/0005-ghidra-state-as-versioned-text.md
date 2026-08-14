# Ghidra 狀態以文字快照進版控

Ghidra 專案本身不進版控，但其分析成果——function 清單與簽章、calling convention、pool 標記、plate comment、struct 與 enum 定義、label 與 global 命名——定期匯出成純文字檔進版控。匯出時機與 commit 綁定：每個工作段落結束要 commit 時，同時匯出一次。

## Considered Options

整個 Ghidra project 目錄進版控不可行——它是二進位資料庫，會讓 repo 膨脹且無法 diff。

完全不進版控（前作的做法）則造成一個結構性問題：前作的知識庫大量以「即時查 Ghidra，不從本表抄」的方式寫作，等於把 Ghidra 當成唯一真相來源，卻沒有版本歷史，無法回溯任何一項分析結果是何時、因為什麼改變的。

## Consequences

「程式碼、知識庫、Ghidra 三者保持同步」這個要求因此變成可驗證的——一次 commit 的 diff 同時涵蓋三者的變化。快照也順帶是 Ghidra 專案的備份。

代價是每次 commit 多一道匯出步驟，且快照檔本身會產生大量 diff 雜訊。這是為了可追溯性接受的成本。
