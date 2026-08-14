# 票 01 的 code review 修正

對票 01（commit `1bc0854`）跑 code review，拿到 7 條發現，全是文件層面的。逐條驗證後都成立，但其中兩條我第一次改的方向是錯的，讀了前作 playbook 才修正回來——這篇的重點在那裡。

## 章號與章節索引差 1

最要緊的一條：`chapters/_index.md` 用第 1 至 30 章（1 起算），而票 08 已經 commit 的 `program_info/cd_audio.md` 用章節索引 0 到 29（0 起算），兩份文件都沒說明彼此的關係，`CONTEXT.md` 也沒有詞條可查。

這不是純粹的措辭問題。`cd_audio.md` 寫著「章節索引 >= 18 需要光碟 2」，照這句去寫 `ch18.md` 的人會把索引 18 的資料記進第 18 章，但索引 18 其實是第 19 章。錯誤不會有任何症狀，只會靜靜地擴散到 30 個章節檔。

處理方式是兩層。`CONTEXT.md` 加「章號」與「章節索引」兩個詞條，把 1 起算 / 0 起算寫進定義本身；`chapters/_index.md` 的章節總表加一欄章節索引，兩種編號併排列出來，需要換算的時候查表就好，不必心算。

寫完總表後順手在檔尾補了一句「光碟分界落在章節索引 18」，補完才發現這正是把 `cd_audio.md` 擁有的事實抄了第二份——才剛要修單一擁有者的問題，自己就先犯一次。刪掉了。總表既然有索引欄，讀者自己換算得出來。

## 兩條改錯方向的

review 說 `chapters/_index.md` 宣告自己擁有「章號與資源 entry index 的對照表」，跟 `cd_audio.md` 已經持有的章節音軌表撞在一起；又說 `assets/_index.md` 規定 struct 定義放 `tables/`，但那個資料夾根本沒建，規則無法執行。

第一次的處理是把兩者都刪掉：拿掉 chapters 的 entry index 職責，並把 struct 定義改判給 `resource_info/`，理由是 `assets/_index.md` 自己第 5 行就說格式歸 `resource_info/` 管。

改完做殘留引用檢查時，`docs/research/fd2-playbook.md` 跳出來兩處命中，讀了才知道方向錯了。playbook 記載前作的 `chapters/_index.md` 職責明確包含「章號 ↔ 資源 entry index 對照表」，而 `assets/tables/` 在前作是真實存在且運作中的結構（playbook 甚至引用得到 `assets/tables/_index.md:39`）。票 01 這兩條規則是照前作抄的，不是憑空發明，而我在沒讀來源的情況下把一套驗證過的架構推翻了。

改回來，但保留 review 真正有價值的部分：

- `chapters/_index.md` 恢復 entry index 的職責宣告，另外補一段界線——從機械碼讀出來的常數表（音軌表這種）歸 `program_info/` 的子系統檔，這裡擁有的是關卡與資源檔 entry 的對應。歧義是真的，但解法是劃清界線，不是刪掉職責。
- `assets/tables/` 實際建出來，寫了 `_index.md` 說明職責。review 抱怨的是規則指向不存在的路徑，那就把路徑建出來，而不是廢掉規則。

**教訓：改架構決策之前先查它的來源。** review 指出的症狀是真的，但它看不到 `docs/research/` 裡的來龍去脈，開出的處方不一定對。這次是殘留引用檢查誤打誤撞救回來的，下次應該在動手前就去翻 playbook。

## 中文數字那條

devlog 原文寫「有兩處 `第二十八章`、`第十六章`」被排除。實際重抓一次攻略頁來數：`第N章` 共 33 處，阿拉伯數字 30 處剛好是 1 到 30 連續，中文數字 3 處——`第十六章` 出現兩次、`第二十八章` 一次。原文列了兩個詞就寫成兩處，漏算了重複出現的那次。

抓頁面時還踩到一個小坑：預設用 Big5 系列編碼去 decode 全部失敗，看了 raw byte 才發現這個站是 UTF-8 with BOM。

數字改成 33 / 30 / 3，順帶把「30 章連續無缺」的推導寫完整。這篇 devlog 是 30 章這個結論的唯一驗證記錄，數字對不上的話以後沒辦法重新核對。

## 其餘三條

- README 的結構表寫了「（共 30 章）」，跟它自己上面八行才宣告的單一擁有者原則打架，拿掉。
- README 漏了 `.scratch/`——它有進版控，裝著 spec 與 25 張工作票，而 CLAUDE.md 要求每個新 session 先讀 README 認識結構，等於整個工作票追蹤從文件入口找不到。補上一列。補的時候本來寫了「共 25 張」，想到跟「共 30 章」是同一種錯，又拿掉了。
- `devlog/_conventions.md` 整份跟 CLAUDE.md 的「Devlog 規範」實質重複且互不引用。正典定在 `_conventions.md`（它的細節比較全），CLAUDE.md 縮成核心三條加一個 `@devlog/_conventions.md` 指向。

## 檢查

寫了一次性的連結檢查腳本掃過全 repo 的 markdown 內部連結，本次改動沒有壞連結。唯一命中的是 `.claude/skills/ghidra-usage/README.md` 指向不存在的 `archive/`，那是 skill 從外部帶進來時沒跟著的封存目錄，與票 01 無關，沒有動它。

這次沒有動 Ghidra，沒有快照要落地。
