# 2026-08-28 票 20：自我回歸閘

## 先查前作

CLAUDE.md 的第一條工作步驟就是查前作，這次查得很值得。`fd2-anatomy` 的 `tools/src_refine/` 底下有兩支現成的東西：`hash_check.py`（sha256 對基準值）與 `eqcheck.py`（重定位感知的功能等價比對），`rebuild_info/verification.md` 有 KB 級的說明，`data/baseline_hash.txt` 還帶著整條基準值推進鏈的註解。spec 的「可沿用前作的資產」本來就列了「LE fixup 感知的比對工具、SHA-256 gate」，這次是把那句話兌現。

三個名字像但性質不同的東西要分開：`tools/code_emit/build_test.py` 叫 build gate，但它只是 DOSBox-X 編譯連結執行的 0-error/0-warning 閘，不比對 baseline binary；`tools/data_emit/verify_real.py` 是 data emit 的 byte-equality 憑據。真正對應票 20 的是前面那兩支。

沿用的是**判定的想法**，不是檔案：前作兩支腳本是兩個獨立的 CLI，各自硬寫自己的 baseline 路徑與 EXE 路徑，而票 20 要的是「可在建置腳本中被前景呼叫並回傳結構化結果」的單一閘門，還要能隨著重建推進換建置目標。所以重寫成 `lefixup.py`（解析與指紋）加 `gate.py`（閘門），前作的兩級判定原封搬過來。

## LE 解析：兩個地方沒照抄

前作 `eqcheck.py` 的 `SRC_SIZE` 表寫的是 `{0x00:1, 0x02:0, 0x05:2, 0x06:4, 0x07:4, 0x08:4, 0x09:6}`，而且 `SRC_SIZE.get(stype, 4)` 對沒見過的型別默默給 4。對照 LE/LX 規格，0x06 是 16:32 pointer 應該是 6 byte、0x08 是 32-bit self-relative 是 4、根本沒有 0x09。這在 FD2 上不會出事——實際掃過去 FD2 的映像只用 0x07——但一個「遮錯寬度就悄悄比對到錯的 byte」的表放在閘門的核心不能接受，所以照規格重寫，未知型別直接丟例外而不是猜一個。

實測四份映像的型別分布：`SMOKE.EXE` 707 筆全是 0x07，`AILSMOK.EXE` 2,416 筆全是 0x07，原版 `FDPS.EXE` 與 `FDPS.LE` 各 6,850 筆，其中 6,849 筆 0x07 加**一筆 0x02**（16-bit selector）。那一筆正好落在前作表裡寫成 0 的那個型別上——照抄的話原版那一筆的 2 個 byte 就不會被遮。目前的閘門不比對原版，所以不會出錯，但這是「照抄會埋一顆雷」的具體例子。

第二個沒照抄的是驗證：前作是一路 `pos += ...` 走完就算數。改成每一頁走完之後檢查 `pos == end`（頁的 record 範圍由 Fixup Page Table 給），走歪了立刻丟例外。這條在四份真實映像上全部通過——但它只證明**長度**對，不證明欄位順序對，下面「code review 抓到兩個從前作繼承的解析 bug」那一節就是被它漏掉的東西。

## 那個超出檔尾 2 byte 的 fixup

`SMOKE.EXE` 的最後一個 fixup site 起於 55,114，寬 4 byte，而檔案只有 55,116 byte。一開始以為是頁號算錯了。查下來不是：LE 的 `data_pages_off` 是**從檔頭起算的絕對位移**（0x4000）而不是相對 LE header（0x2a50），照絕對讀，最後一頁的結尾 16384 + 9×4096 + last_page_size(1868) 正好等於檔案大小 55,116，分毫不差。所以映射是對的——真的就是最後一筆重定位跨過了 `last_page_size` 的截斷點，剩下的 byte 由載入器補零。`AILSMOK.EXE` 也有（超出 1 byte），原版沒有。`blank()` 於是要夾住範圍，而不是把它當成映像壞掉。

## 閘門的閘門

前作 `_index.md` 的硬性注意寫得很清楚：永遠不會 FAIL 的檢查在壞掉的東西上也會說 PASS。這次把 selftest 做成用**自己合成的一份 LE 映像**：頁數、每頁的 fixup 位置、record 內容全部預先安排，解析器必須報出完全相同的位置清單。這樣不需要 DOSBox-X、不需要任何不進版控的產物就能跑，也讓「解析器對不對」變成對照 ground truth 而不是對照它自己。

五種判定各由「該判定所描述的那種改動」實際產生一次：交換同一頁裡兩筆等長的 record（fixup 重排的精確模擬）要得到 `strict`、翻掉某個 site 的 byte 要得到 `reloc`、翻掉一個不被任何 site 覆蓋的資料 byte 要得到 `different`、截掉 1 byte 要得到 `size`。解析器的負面案例兩條：塞一個不存在的 source type、以及硬給某筆 record 加上 source-list 旗標讓記錄流錯位，兩者都必須丟例外。

然後再拿真的映像跑一次真的負面測試：把 `smoke.c` 的 `VFS_SIG_OFF` 從 `0x0b` 改成 `0x0c` 重建，大小一模一樣、警告零、錯誤零，閘門判 `different` 並回離開碼 1。這比合成映像更有說服力，因為它證明「一個 immediate 的改動」確實落在所有重定位之外。

過程中還意外測到另一件事：第一次做這個負面測試時用 PowerShell 的 `Set-Content -Encoding UTF8` 改檔案，寫進了 BOM，結果 `wcc386` 直接在 `stdio.h` 裡爆出 20 個 `E1127: Type required in parameter list`。閘門照樣正確報 FAIL 並把錯誤行印出來，算是免費驗證了 errors 那一項。教訓是改原始檔一律走 Python 的 byte-level 讀寫，不要用 PowerShell 的文字 cmdlet。

## 死路：想實測 reloc 那一級在本專案走不走得到

`strict` 與 `reloc` 兩級都是前作在數百個 object 的連結上實測出來的（Watcom 9.5a）。想確認在 10.0a + wlink 10.0 上、在本專案自己的產出上也走得到，於是做了個實驗：往 `smoke.c` 塞兩個被引用到的 tentative definition 全域（`gate_probe_aaa` 與 `gate_probe_zzz`），建置一次；再把前者改名成 `gate_probe_mmm`（同長度，只換排序位置），建置第二次，比對兩份映像。

結果是 `identical`——五個欄位全等。也就是說單一 translation unit 的情況下，wlink 保留 COMDEF 的定義順序，改名完全不動輸出。這不能推翻前作的結論（前作是幾百個 `.obj` 的連結，跨模組的 COMDEF 才會被依名重排），但它說明**今天這個專案的建置規模還碰不到那兩級**：目前兩個目標的建置都會落在 `identical`。兩級留著是因為 Phase C 之後會變成幾十個 `.obj` 的連結，那時才是它們該起作用的地方；在那之前，它們的正確性由 selftest 的合成映像保證，不是由真實建置保證。

沒有繼續往「造一個多 TU 的實驗」推，因為那要為了一個不影響任何決策的問題再搭一套建置。結論記在這裡就夠了。

## Code review 抓到兩個從前作繼承的解析 bug

上面那句「照規格重寫」寫得太早了——第一版只改了 `SRC_SIZE` 表，記錄的欄位順序還是照抄前作的。review 對著 LE/LX 規格逐欄比，抓出兩處：

**source list 的偏移量清單位置錯了。** 規格是 `SRC, FLAGS, CNT, OBJECT, TRGOFF, [ADDITIVE], SRCOFF1..n`——偏移量清單在記錄的**最後**，在 target data 之後。前作（與第一版）在讀完 `CNT` 之後就緊接著讀 `count` 個 int16，於是把 OBJECT／TRGOFF／ADDITIVE 當成了來源偏移，又把真正的偏移清單當成 target data 跳過。惡毒的地方是總長度剛好一樣（`3 + 2n + t` 對 `3 + t + 2n`），所以我引以為傲的那條 `pos == end` 檢查完全查不到——它只證明長度對，不證明欄位對。真的踩到時，`blank()` 會抹掉一堆亂七八糟的位置：真的改到 code 的地方沒被比到而判成通過，純粹的重定位位移反而破掉殘差而判成回歸，兩個方向同時錯。

**import by ordinal 的序號寬度整個反過來。** 規格是 8-bit ordinal flag（`0x80`）→ 1 byte、否則 32-bit target offset flag（`0x10`）→ 4 byte、否則 2 byte；程式寫成 `4 if 0x80 else (2 if 0x10 else 1)`。旁邊 internal／by name／entry table／additive 四條分支對照同一份規格都是對的，看起來就是抄寫時翻掉了。這個是會大聲壞掉的：走位一錯就撞到頁邊界檢查丟例外。

兩種記錄形狀在手上四份映像裡**一筆都沒有**（全部是 `0x07` internal、沒有 source list），所以現在不會出事，是等 Phase C 變成幾十個 `.obj` 的連結之後才會爆的地雷。這正好說明「拿真實映像跑過就算驗證」的極限——真實映像只覆蓋它自己用到的那一種形狀。

修法是把兩處照規格改對，然後把合成映像擴充成**帶著這兩種形狀**：兩筆 source list（一筆含 additive）、三筆不同序號寬度的 import by ordinal，全部有預先安排的 ground truth。再把兩個 bug 各塞回去跑一次，確認新測項真的會 FAIL——source list 那個是「位置對不上」的 FAIL，ordinal 那個是丟例外，順手把 selftest 的 ground truth 那一行包進 try 讓例外也變成一列 FAIL 而不是整個 traceback。

## 同一輪 review 的另外幾項

- **`lefixup.profile()` 沒有保護。** 產出被截斷、或未來某個目標連成別的容器格式，閘門會丟 traceback：`result.json` 不會寫、後面的目標不會建、`import gate; gate.check(...)` 拿到例外而不是 `{"verdict": "FAIL"}`。契約自己毀掉。改成接住 `LeError`／`struct.error`／`OSError` 記成一項失敗的 `image` 檢查；`compare()` 對手改過的 baseline 也接 `KeyError`。
- **光碟的判定比測試套件自己的判定寬。** 閘門只看 `.cue` 存不存在，`build_min.py run` 的前置檢查還會去解 cue 指名的 `.bin`。懸空的 cue 於是會讓閘門認定「有光碟」→ 跑套件 → 套件自己 exit 1 → 報成回歸，而不是照設計報 skip。改成用同一條件（`bm.cue_bin`）。
- **建置到底掛不掛光碟。** `_index.md` 寫「不掛」，程式其實掛了。想了一下該改哪邊：`build_pipeline.md` 的既有決定是兩個階段共用同一份掛載定義以免漂移，而閘門必須用**與正常建置相同的方式**建置，否則它閘的不是會出貨的那個流程。所以留著掛，改文件。
- **測試套件不理會 `--target`。** `--target smoke` 會跳過 AIL 的前置檢查，然後照樣去跑 `ail_link.selftest`，在沒有 `libs/ailv3/ailv3.lib` 的 checkout 上直接把 smoke-only 的閘門判 FAIL，還順便重建兩次 `AILSMOK.EXE`。每個套件加一個 `target` 欄位就解決。
- **警告檢查的失敗訊息可能什麼都沒說。** 判定是「沒有新文字 **且** 摘要數量沒上升」，訊息卻只由前半段組出來；同一個警告多響一次會得到 `FAIL warnings: 0 new: none`。訊息改成兩邊都報。

## 基準值格式

前作把推進鏈寫成 `baseline_hash.txt` 的 `#` 註解，一則一則往回串，人讀起來很好，但機器讀不到。改成單一 `data/baselines.json`：一個建置目標一筆，`history` 是陣列，每筆帶日期、commit、推進理由、可接受的警告文字、以及重定位感知的指紋。推進的指令強制要 `--reason`，而且建置有錯誤或未解析符號就拒絕記錄——基準值只能從乾淨的建置取得。

警告的比對用**文字集合**而不是數量，理由是數量相同也可能是換掉了一個警告。目前兩個目標的可接受警告都是空集合。

前作的 `baseline_hash.txt` 註解引用了 `rebuild_info/equivalence/neighbour_reads.md`，那個檔已經併進 `rules.md` 了，是個未同步的懸空引用。本專案的推進理由寫在 JSON 裡、不引用外部檔名，剛好避開同一個問題。

## 目前的狀態

兩個建置目標（`smoke`、`ailsmoke`）都記了基準值，`check` 全綠：兩個目標都 `identical`，五個測試套件（閘門自己的 selftest、`fdps_build.selftest`、`ail_link.selftest`、`fdps_build.run`、`ail_link.run`）全過。`ail_link.run` 要 `--with-audio` 才跑，因為它會開視窗又需要不進版控的 `fdps_game_files/`；不跑時會被列進 `skipped_tests`，不會靜默消失。

Ghidra 這一票完全沒有動到，所以沒有快照 diff。
