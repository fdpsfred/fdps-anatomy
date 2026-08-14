# 2026-08-14 知識庫骨架與 devlog 建立（票 01）

票 01 要求「專案有一個可以放東西的地方」。實際做的事情不多，但有兩個地方花了時間：資料夾命名要不要沿用前作，以及關卡單位到底是不是 30 章。

## 資料夾命名：直接沿用前作

票上寫的是五個視角的中文名（程式、資源檔、資料表、關卡、重建），沒有指定資料夾名。想過要不要用更直白的名字（`code/`、`files/`、`data/`、`levels/`、`build/`），最後決定原封不動沿用 FD2 的 `program_info/` `resource_info/` `assets/` `chapters/` `rebuild_info/`。

理由是 `docs/research/fd2-playbook.md` 對這五個資料夾的職責切分寫得很細，而且 playbook 全篇都用這組名字在引用前作的檔案。換名字的話，每次讀 playbook 都要在腦裡做一次映射，成本會一直付下去。`assets/` 這個名字對「資料表視角」其實不太貼切（前作的定位是「玩家視角」），但這個代價比命名不一致小。

`program_info/` 與 `resource_info/` 在票 08 就已經建好了，這次只補上另外三個。

## 關卡單位：30 章，靠攻略站確認

一開始直接照 CLAUDE.md 記的攻略站網址 `.../c31/fdps/fdps/` 抓，拿到 404。試了 `index.htm`、`index.html` 都是 404，一度以為站台掛了。

實際情況是：`.../c31/fdps/` 才是可以拿到東西的路徑，它是一個 frameset，裡面指向 `fdps/menu.htm` 與 `fdps/notes.htm`——所以內容頁確實在雙 `fdps` 目錄底下，CLAUDE.md 的註記沒錯，但那個目錄本身不可瀏覽，只有具名的 `.htm` 檔能拿。已經把這件事補寫進 CLAUDE.md，免得下次再撞一次。

`menu.htm` 列出 9 個內容頁，跟 spec 記的「9 個內容頁」對得上。從 `fdps.htm`（遊戲攻略）抓 `第N章` 的出現，得到第 1 章到第 30 章連續無缺，沒有序章、終章、尾聲之類的額外單位。順帶把 30 章的標題一起抄進 `chapters/_index.md`，之後每章檔案的檔名與對照都有依據。

有兩處 `第二十八章`、`第十六章` 是內文裡用中文數字寫的敘述，不是章節標題，數的時候排除掉了。

## 其他

- `devlog/runs/` 是空的，放了 `.gitkeep` 讓它進版控。之後 workflow 的 agent 回報 JSON 歸檔到這裡。
- `.gitignore` 原本就涵蓋 `workspace/` `legacy/` `fdps_game_files/`，這次確認過 `git check-ignore` 三條都命中，沒有改動。
- README 裡 `src/` 那一列刻意不加連結——資料夾要到 Phase B 建最小骨架時才會出現，先加連結會是壞連結。

這次沒有動 Ghidra，所以沒有快照要落地。
