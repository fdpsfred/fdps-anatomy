# 27 — 回歸閘跳過對齊填充

**What to build:** `game` 目標只改註解、巨集名或換行字元時不再報 `different`；真正改到 code 或 data 的改動照樣報出來。基準值在任何工作目錄狀態（主工作目錄、乾淨的 git checkout、worktree）都重現得出來。

**已知事實**（見 `rebuild_info/build_gate.md`「第三個行為中性的效果」與 `devlog/2026-09-25-kb-and-source-fixes.md`「回歸閘的 `different`」）：

- `wcc386` 不把對齊填充清零，填的是編譯器緩衝區的殘值，會跟著原始碼文字變：只改註解就會變，同一份原始碼換了換行字元（git 取出的 CRLF 與 agent 直接寫檔的 LF）也會變。沒有任何程式讀這些 byte。
- 兩種位置：`CONST` 字串池裡每個字面值結尾 NUL 之後補到 4 byte 邊界的 1–3 byte；`_DATA` 裡小物件（例如 `unsigned char`）後面到下一個對齊邊界的空隙。
- 票 24 記的 `game` 基準值在乾淨 worktree 重建就對不上（主工作目錄混有 LF 檔）；票 25.15 推進的新基準值同樣建在主工作目錄上，乾淨 checkout 很可能又對不上。
- 其餘目標：`emittest` 按設計不比映像；`smoke` 不編 `src/`；`ailsmoke` 只編 `src/dpmi.c`、`src/ailflags.asm`，同樣受這個效果影響但範圍小。
- 票 25.15 的 `tools/build_gate/pad_diff.py` 已能比兩份映像：字面值填充自動認定，其餘差異列出兩側的 map 符號讓人判斷。閘門本身還沒有用它。

**做法方向：** 閘門依 linker map（`FDE.MAP`）與物件的宣告大小算出每個對齊空隙，比對前把兩側的空隙 byte 抹零，再走既有的五種等價判定。已有的 fixup 抹零（`lefixup.py`）是同一種手法。空隙的判定要嚴格：只抹「前一個符號結尾到下一個符號起點之間、且小於對齊單位」的 byte，不能把整段不明區域都放過。

**Blocked by:** None — can start immediately

**Status:** done

**前作：** 查過 `C:\Users\fdpsf\Documents\fd2-anatomy` 的 `tools/`（`fd2_build`、`fd2_diff` 等）與知識庫，前作的閘門沒有處理對齊空隙，無可沿用；重定位抹零的手法沿用本專案 `lefixup.py`（其來源是前作）。

- [x] 閘門比對 `game`（與 `ailsmoke`）時抹掉對齊空隙；判定結果多一種能說明「只差填充」的等價等級，或併入既有等級並寫明——新等級 `pad`（`tools/build_gate/lepad.py`；`ailsmoke` 的連結改為寫出 map）
- [x] 證明：只改一行註解、只改換行字元，閘門不報 `different`；改一個常數或一個字串內容，閘門照樣報出來（單元測試或固定的 fixture）——`lepad.py selftest`（合成的建置，排進測試套件）加上乾淨 worktree 的逐項實建，見 `devlog/2026-09-26-gate-alignment-gaps.md`
- [x] 在乾淨 worktree 以 `HEAD` 重建，與主工作目錄的建置判定為等價；必要時以新的判定重記基準值，理由寫明——`game` 與 `ailsmoke` 以帶空隙指紋的新判定重記
- [x] `rebuild_info/build_gate.md` 改寫「第三個行為中性的效果」與基準值更新規則表對應的列，`pad_diff.py` 的角色（併入閘門或保留為診斷工具）寫明——`pad_diff.py` 改寫成與閘門同規則的診斷工具

**結果：** 實測觸發殘值改變的是標頭檔的換行字元；在 `.c`／`.h` 加改註解、改巨集名、`.c` 換成 LF，映像都逐 byte 不變（票面「只改註解就會變」不成立）。
