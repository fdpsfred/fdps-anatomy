# 票 25.17 第二段：兩支 workflow 的落地、拒收的修正與 outside 修正

主 session 代跑了逐文件驗證（110 段，findings 450，第二位 agent 確認或修改後落地 424 筆，拒收 0，閘門全過）與跨文件一致性（353 項，落地 199 項的修正，閘門全過，但拒收 64 筆）。落地結果先原樣 commit（`d79299d`），再處理拒收與 outside。

## 拒收的修正

一致性段落地時，好幾個項目改的是同一段文字：例如「成長上限是後一個值減 1」那一句，X13、X35、X36、X37 各寫了一版，第一個落地之後其他幾版的 `old` 就不存在了；X30 把「神秘商店」統一改成「秘密商店」，33 筆修正裡有些句子已經被別的項目先改寫。

先想做確定性的分流，碰到一個自己埋的坑：`kbconsist.py apply` 在判定檔上記的 `_refused` 只寫了哪一頁、為什麼，沒寫是哪一筆修正。所以拒收紀錄（93 筆）對不回修正本身。改用現況反推：對每個有拒收紀錄的項目，把它每一筆 `old` 已經不在頁面上的修正都拿出來（108 筆，其中含同一項目裡確實落地了的那幾筆）。新文字已經逐字在頁面上的 53 筆，不論是它自己落地的還是別人寫了一樣的字，都判為已涵蓋，逐筆記在 `workspace/kb_refused/triage.json`、收尾報告照列；其餘 55 筆一筆一個 agent（`refused_ticket25_17.js`），判斷現行文字是否已經說出那筆修正要說的事實，不是就對現行文字寫修正。機制沿用 `kbconsist.py`（加 `use_workspace` 換項目清單與判定檔），不另寫一套 gate。

主 session 回報的是「64 筆」，與 93／108 對不上，是因為落地 agent 回報的是它讀到的條數；判定檔上的紀錄才是準的，devlog 與收尾報告以判定檔為準。

R3a 與 R3b 在這裡撞在一起：R3a 判 `FDETXT30` `0x13` 應從 S4 移到 S13b（舊稿），R3b 判 `0x14` 應從 S13b 移到 S4——一個把前半推向舊稿、一個把後半推向殘留內容，兩者合起來是把原本的分界整個反過來。它們各自的 `story.md` 修正有一部分被拒收，`story.py` 的 `OWNERS` 改動則都在 `outside`。讓它們在第四段落在同一組（都提到 `tools/cut_content/story.py`），由同一個 agent 對照原文一次定案。

## outside 修正

兩份收尾報告的 `outside` 合計 145 條：`src/` 註解的錯（同一句錯註解常被三、四個驗證者分別看到，例如 `src/text.c` 的「reads six bytes in front of the font sheet」、`src/chevt6.c` 把白骨戰士與死靈寫反）、產生器的手寫資料（`story.py` 的「神秘商店」、章節判定檔的 why）、別的頁、工具的 docstring、票、ADR、Ghidra 的參數名與 plate。

一條一個 agent 會讓四個 agent 各改同一段註解，落地時又全部互相拒收——和一致性段剛踩過的坑一樣。所以改成分組：每條提到的非知識庫檔案做 union-find，只提到知識庫頁的依第一頁。第一次寫的路徑正規表示式用了非貪婪加上 `js` 排在 `json` 前面，把 `manifest.json` 截成 `manifest.js`，測試抓到後改成貪婪並把較長的副檔名放前面。分出 66 組，最大一組 20 條（第 1–24 章處理函式的註解散在 `chpost1.c`、`chend2.c`、`chinit1.c`、`church.c`、`unitstat.c` 幾個檔，彼此以檔案串起來）。

C 原始碼只准改註解。`build_emit.strip_c` 會連字面值一起抹掉，所以不能只比片段；做法是把整個檔改前改後都 `strip_c`，要求 token 序列相同，或只差識別字（巨集改名是註解錯誤的延伸，`chevt6.c` 的兩個巨集名稱就跟著白骨戰士／死靈寫反了）。字面值的改動這一道看不到，靠落地後的完整建置閘門抓。

章節判定檔改了 why 之後，落地頁的產生區塊要重產，原本沒有指令能做這件事（`land.py` 從草稿落地，`--landed` 只比對），在 `check_chapter.py`（`fill` 的擁有者）加 `--refill N`，只重產區塊、不動敘述；在現行頁面上跑一次確認沒有任何變動。

## 兩支 workflow 跑完之後

拒收段 55 項全部定案，落地 15 項的修正（其餘判為現行文字已經說出那筆修正要說的事），拒收 1 筆：F47。F47 與 F2 改的是 `pitfalls.md` 同一列「影片播放不在重建範圍」，F2 先落地。逐項比對 F47 要補的六個步驟（關掉 AIL、卸下鍵盤中斷、調黑、還原 DAC、裝回鍵盤中斷、重新初始化音效），現行那一列逐字都有，還多了 F47 沒寫的清空鍵盤緩衝與停 CD 音軌；F47 另外想把第一欄改成「……但呼叫前後的拆裝在」、補一句「否則 AIL 計時器與鍵盤中斷仍掛在遊戲身上」，前者現行那一列的「這些收拾不能因為播放不在範圍內就省掉」已經說了，後者是沒有證據的推論（沒有人實測過不拆會怎樣）。判為已涵蓋，不另開 workflow。

outside 段 66 組定案 65 組，落地 202 筆修正（`src/` 與 `tests/` 註解、工具的說明與手寫資料、`story.py` 的歸屬表、兩份章節判定檔），重產與全部閘門都過，完整建置閘門的遊戲本體判定是 `pad`（差異全在 `wcc386` 不清零的對齊空隙裡，其餘位元組不變；落地時改寫的標頭檔換行字元是已知的觸發者，見 `rebuild_info/build_gate.md`，這次沒有再追是哪一個檔），另兩個目標 `identical`。沒過 gate 的是 O50：它要改 `judgements/ch06.json` 那一條 why，但同一條已經先被 O2 組（X53）改寫成「兩份只差『每個人的一生中』那段的說話者」。O50 要說的事實（不是副本、只差說話者）現行文字已經有，只少了說話者是誰（法蓮娜對尤利安）；那是細節不是更正，判為已涵蓋。第 6 章的產生區塊已由落地段 `--refill 6` 重產，`--landed-all` 與 `data_skill` 都過。

R3a 與 R3b 在 O2 組定案：`FDETXT30` `0x13`、`0x14` 兩條都是道別那一場的舊稿（顯示版 `FDETXT64` `0x11`–`0x16` 以逐字相同的開頭接續、同一個腳本位置、共用「我會永遠記得妳的。」），同歸 S13b；R3b 把 `0x14` 移進 S4 的提案不成立。四個 agent 各自獨立給的理由相同，但信心都是 medium——「同行邀約改寫成再會約定」算同一場景的改寫還是另一段劇情，是分類判斷，列進交給開發者的問題。

## Ghidra

outside 段的判定裡有 19 項 Ghidra 改動：兩個參數改名（`fdps_audio_init` 的 `param1` → `tick_rate_hz`、`fdps_blit_dispatch` 的 `arg6` → `mode_operand`，與 `src/` 的名字相同）與 17 項 plate。plate 的改動寫成「把某句換成某句」的散文，逐項由 MCP 取出整段 plate、手動重打再寫回太容易抄錯，所以寫了 `ApplyPlateEdits.java`：讀一個 TSV（位址、模式、base64 的舊文與新文），`replace` 要求舊文恰好出現一次，`full` 要求現有 plate 與快照裡的完全相同，前提不成立就拒收。散文裡的引號抽取第一次用單引號的正規表示式，碰到 `boss's`、`fdps_blit_dispatch's` 這種帶撇號的字就切錯位置，改成把這幾項的新舊文照判定原文逐字寫出。25 筆全部套用、0 拒收；之後 `Bad Instruction` bookmark 0、兩支改了參數名的 function 仍是 `__cdecl`，存檔並匯出快照（`comments.txt` 與 `functions.txt` 有 diff）。

## 還沒收的問題

四份收尾報告除了修正以外，還有十來條「驗證者看到了、但不在自己那一項裡所以沒處理」的問題：`build_flags.md` 的 2,587 個推送點沒涵蓋 CD 與 DPMI 那一段、`characters.md` 說攻略站的升級成長與資料逐筆相符但英雄的 HP 下限不符、`spell.md` 與 `battle.md` 的驗證對象重疊七支、`src/aitarget.h` 還是「bit 0x10 加低 4 位」的舊說法、`ch01.md` 第 1 步與第 3 步的先後矛盾、`data_structures.md` 的 `long_double_80` 所在欄、AIL 的 Function ID 數字是舊邊界的、`routing.json` 沒人讀的 status 欄等。原本想直接列給開發者，但它們多數是查得到答案的事實，不是只有開發者答得了的問題；照 CLAUDE.md，工作結束前 backlog 要處理掉。沒有自己逐條判斷，而是逐條寫成請求（`followups_25_17.json`），讓 `kbfollowup.py` 套用第四段的機制（分組、gate、回掃、落地、完整建置閘門）再跑一輪。`spell.md` 的「不可通行旗標擋住擴散」一條在寫請求前查過已被別組改掉，沒有列進去。

第五段跑完：10 組全部定案，8 條修正、3 條查證後已涵蓋（`build_flags.md` 的推送點範圍與 DPMI 旗標、經驗懲罰的界線，頁面原本就寫對了），落地 10 筆，完整建置閘門 `game` 仍是 `pad`。AIL 的 Function ID 數字照目前邊界重算成 390／7／39——原本擔心要重跑 Ghidra 的查詢，agent 改從程式庫裡沒有同長度的 body 推得合併後的 `000447a6` 不會命中，所以原本列給開發者的那一題撤掉。`routing.json` 的 status 欄查證沒有讀者，但 agent 沒有拿掉它：重產 `routing.json` 不在允許的重產清單裡，拿掉產生器的欄位卻不重產會讓產生器與輸出不同步，所以改在產生器與 `_index.md` 寫明它是固定值、沒有讀者。

它自己又留下三條組外的：`src/chinit1.h` 三處「名冊加入必須在重置之前」的註解（實際約束是在最後一次依名冊重建之前）、`src/spellmnu.c` 同樣的「低 nibble」說法、`data_structures.md` 的 `L$N_emu387_state` 與 `L$N_emu387_extended_real` 兩列（Ghidra 裡沒有套用）。用同一份請求檔再加一條、`freeze --refresh` 重新分組會讓第一批十組的判定全部對不上現行項目而變成過期，所以改成第二批一個檔、一個工作區（`kbfollowup.py --batch 2`），交 `followup2_ticket25_17.js`。

第二批跑完：3 條全部修正（`chinit1.h` 只改第 1 章那處，第 2、3 章的開場過場不切地圖、原註解本來就對；`spellmnu.c` 的註解；`data_structures.md` 兩列照 Ghidra 現況改寫，Ghidra 不動，因為 `source_operand` 的 plate 已說明它刻意維持獨立物件），完整建置閘門 `game` 仍是 `pad`。

O2 的第二位讀者（直接讀 `0x27c20` 的反組譯：`CMP [EBP-0xc],0xf`／`JLE`，直線分支 `SUB EAX,0x10`，全程沒有 `AND 0xf`）指出同樣的「低 nibble」說法還在 `src/spellmnu.h` 與 `src/aiact.h` 各一處，都在它的組外。這個事實已由第五段 O7 與第二批 O2 兩組獨立判定確認過，主 session 決定不再開 workflow，由票的 session 照已確認的事實直接改這兩處註解（出處：第二批 O2 的判定），改完跑完整建置閘門：`game` 為 `pad`，`ailsmoke`、`smoke` 為 `identical`，全部測試套件通過（`ail_link.run` 因本機沒有音效環境照例略過）。改的時候以 `bit 0x10`、`low nibble` 再搜一次 `src/` 與 `tests/`，找到三處同類的說法：`src/table.h` 的 `cast_range_flags` 說明寫「straight-line bit 0x10」、`tests/aiact.c` 的測試註解寫「bit 0x10 marks the shape and the low nibble is the beam's length」、`tests/table.c` 寫「the straight-line bit 0x10 over a range of 7」；另外 `src/aiscore.c`、`src/item.c`、`src/spellmnu.c` 的巨集名 `*_LINE_BIT` 本身就帶著「位元」的暗示。照主 session 的指示不再擴散，只列給主 session。

主 session 看過之後要一次收掉：同一個事實（距離 byte 與 0x10 做數值比較並減去 0x10，不是位元遮罩，O7 與第二批 O2 已確認），不另開 workflow。三處註解照這個事實改寫；兩個巨集改名，`ITEM_USE_DISTANCE_LINE_BIT`（`src/aiscore.c`、`src/item.c`）與 `CAST_RANGE_LINE_BIT`（`src/spellmnu.c`）改成 `ITEM_USE_DISTANCE_LINE_BASE`、`CAST_RANGE_LINE_BASE`，值 0x10 不變。名字選 `_BASE` 而不是 `_THRESHOLD`，因為同一個值兩種用法都有：既是分界（`<`／`>=`），又是算直線長度時減掉的基底，`_BASE` 兩種讀法都說得通；前作 FD2 沒有對應的巨集可沿用。改名前查過 Ghidra 快照的 `comments.txt` 沒有引用舊名，巨集是前置處理器的名字、不是 Ghidra 符號，所以不動 Ghidra。改名以位元組層級替換，不動換行字元。之後再搜一次 `src/` 與 `tests/`：`bit 0x10`、`_LINE_BIT` 已經沒有；剩下的 `nibble` 全是 AI 行為 byte（`+0x34`）、調色盤、CD 子通道與地圖標記，都是真的位元欄位，說法正確。`devlog/runs/` 裡的舊判定照原樣保留舊名，那是當時的記錄。完整建置閘門：`game` 為 `pad`，`ailsmoke`、`smoke` 為 `identical`，單元測試映像（含 `tests/aiact.c`、`tests/table.c`）編譯連結並全部通過。

## lint 的剩餘

兩段 workflow 後剩 15 筆，逐筆看過都是正當用法：README 的資料夾表列出 `workspace/`、`legacy/`、`fdps_game_files/`，詞彙表定義 devlog 這個詞，`build_gate.md` 說明基準值「不放在 `workspace/`」，`data_emit.md`／`emit_pipeline.md` 描述管線自己在 `workspace/` 的足跡與 devlog 的 commit 規則，`pitfalls.md` 範例裡的 `fdps_foo`。規則本身刻意粗，所以不改規則，改在 `kbverify.py` 加 `LINT_ACCEPTED`，每筆附理由，lint 歸零。
