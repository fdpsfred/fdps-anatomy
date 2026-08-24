# 票 15：把遊戲邏輯命名成人看得懂的東西

票 14.2 收工時，`pool_fdps` 有 514 個 function，其中 417 個還掛著 `FUN_xxxxxxxx`。這一趟要給每一個語意名稱、逐一判定的 calling convention、語意化的參數名稱與描述行為的 plate comment。跑完之後 `pool_fdps` 沒有預設命名殘留，兩道 gate 全程乾淨。

## 先量現況，發現工作量比票寫的小

票說「票 12 已定案的 109 個只補參數命名——它的判定檔的 `params` 欄位記的是傳參證據，不是語意名稱」。實際去量的結果不是這樣：票 12 的判定檔裡 prototype 本身就帶語意參數名（`void fdps_map_actor_behavior_step(int actor_index, int context)`），而且票 14.2 又替一大批 function 補了簽章，Ghidra 現況裡 `pool_fdps` 的 672 個參數已經有語意名，只剩 138 個是 `param_N`——而那 138 個全部落在還沒命名的 417 個裡。

所以票裡設計的「params_only 輕量路徑」實際上是空的。`build_worklist.py` 仍然把它算出來當一個分類（0 個），因為看到「parameters only 0」才知道這件事被確認過，而不是被忽略。

## 三件在寫 workflow 之前先確認的事

**判定檔不用自己重寫一份證據。** 票 14.2 的 1,352 個判定檔裡有 `role` 欄位與非常詳盡的 plate comment，`DumpNamingState.java` 把它當成 `current_plate` 一起匯出。提示裡明講它是證據不是答案——那張票問的是別的問題（pool、邊界），行為敘述只是副產品——但它讓 agent 省下重新讀懂一支 800 byte function 的第一遍。

**證據每一輪都會過期，這是本票結構上與前幾張票最大的差別。** 命名一個 function 最有用的線索是它已經命名的鄰居叫什麼，而那正是上一輪的產物。票 14.2 的證據（位元組、FID 命中）不會因為別人的判定而改變，本票的會。所以 Apply 階段除了轉錄，還要重跑 `DumpNamingState.java`（20 秒）與 `build_vocabulary.py`，下一輪的 agent 讀到的 `ctx/*.json` 才是最新的。

**工作清單固定，唯一的自由度是順序，而順序有實質影響。** 一個什麼都不呼叫的 function 用自己的位元組就能解釋自己；一個呼叫二十個別人的 function 有一半的內容是那二十個在做什麼。所以 `build_worklist.py` 以 callee 數排序，讓葉子先做。這是票 12 的 BFS 在固定清單下的替代品。

## 踩到的坑

**Workflow 拒收帶 CR 的腳本。** 用 Python 在 Windows 上改 `.js` 檔時預設把 `\n` 寫成 `\r\n`，Workflow 工具回 `script contains control characters that would be hidden in the approval dialog`，整份腳本被擋下。訊息沒有指出是哪些字元，找了一輪才確認是 757 個 `\r`。之後改檔一律 `open(p, 'w', newline='')`。

**thunk 讓撞名檢查誤報。** 第一次跑 `AuditNaming.java` 就抓到 `__sys_init_387_emulator` 同時掛在 `0003d50a` 與 `000444a4`。查票 14.2 的判定檔，那張票已經判定 `0003d50a` 要把名字還回去、改回 `FUN_0003d50a`，於是先補跑了一次它的落地腳本——`renamed = 1`，成功，但再稽核一次還是撞名。

原因是 `0003d50a` 是 thunk：Ghidra 對 thunk function 的 `getName()` 回傳的是**被 thunk 的 function 的名字**，而它自己的 symbol source 是 `DEFAULT`。所以那從來不是兩個位址搶同一個符號，只有一個位址 claim 過它。撞名檢查因此改成看 symbol source 而不是字串——「什麼算是一個人的主張」這個問題，字串答不出來。順帶一提，票 14.2 的落地其實沒漏，是我先誤判了它漏。

**plate comment 很容易引用還不存在的名字。** 手動跑第一個 function（`000109b0` → `fdps_unit_is_retired`）當試驗時，自己在 plate 裡寫了「`fdps_unit_count_by_type_alive` 數的是……」——那是我當下預期 `00018350` 之後會拿到的名字，而那時它還是 `FUN_00018350`。這條規則因此進了提示：引用鄰居用它**當下**的名稱，還是 `FUN_` 就用位址。

## 回掃的觸發條件第一次設得太寬

試驗批 8 個 function，7 個回報 `has_open_question`。照原本的設計，那 7 個全部要進回掃，等於整趟成本翻倍。

去讀那些 open question，發現它們分成兩類，而原本的設計沒有區分：一類是「等鄰居命名之後用字要對齊」（`fdps_get_rgb_blue` 的兩個兄弟還是 `FUN_`），重讀確實會改變判定；另一類是「這個全域的單位是什麼、值域多少」，那不是命名能回答的問題，重讀一百次也只會把同一句話再寫一遍。

於是把「值得重讀」與「有未解問題」拆成兩個欄位，`needs_rescan` 由寫判定的 agent 自己判斷，回掃只收它跟低信心。最後的數字是 417 個判定裡 288 個留有 open question，而真正需要重讀的只有 133 個——差了一倍以上，全部省下來。

## 第三批撞上 session 上限，停止機制照設計動作

第三次呼叫跑完 136 個判定、`pool_fdps` 歸零之後，回掃段有一輪 16 個 agent 全部回傳失敗，訊息是 `You've hit your session limit`。ADR-0007 第 5.2 條的判定條件（該輪回傳數為 0 且送出數大於 0）觸發，workflow 立刻停止，沒有送出任何註定失敗的重試；5.6 條讓收尾成品不被產出——`report` agent 自己也失敗了，但就算它活著也不會寫出半套的知識庫頁面。

額度重置後直接再呼叫一次同一支腳本，不帶 `addrs`：`build_worklist.py` 算出 `full` 是 0（全部有判定檔且 body 雜湊相符），queue 為空，直接進回掃段，把剩下的 42 個做完。**續跑不需要任何額外機制，判定寫檔本身就是那個機制。**

## 陷阱比預期多得多，而且它們的家不在 pitfalls.md

原本以為 `pitfall` 欄位會零星填幾十個。實際上 417 個判定裡 320 個填了——四分之三的 function 有至少一件「照直覺寫就會與原版不同」的事。這個比例本身是個結論：這不是一個偶爾有怪癖的程式，是一個到處都是怪癖的程式。

問題是它們沒辦法全部搬進 `rebuild_info/pitfalls.md`。那份表收的是判斷與對策，320 條 function 層級的細節塞進去會讓它變成一份 function 清單，而且違反「每個事實只有一個擁有者」——這些事實的擁有者應該是那支 function 自己。

處理方式是兩層：個別的細節寫進該 function 的 plate comment 尾端一段 `Rebuild note:`，隨 `ghidra_snapshot/comments.txt` 進版控，寫 C 的人在看那支 function 的時候就會看到；跨 function 反覆出現的模式才進 `pitfalls.md`。前 130 個判定寫的時候還沒有這條規則，用 `merge_pitfalls.py` 補進去（183 個），然後整批重跑落地。

歸納出來的模式有十來個，最密集的幾個：

- **動畫迴圈的 tick latch 在寫入前就被讀取**，所以第一格不等待。十幾支迴圈都是這個形狀，而「在迴圈前初始化」是任何人都會順手做的修正。
- **timer 計數器必須 `volatile`**，否則最佳化器把載入提出迴圈，遊戲在第一個等待點永遠停住。
- **範圍檢查刻意單邊或不對稱**：數值條只壓下界、視野裁切兩軸不同、格子繪製四邊全嚴格且不合格就整塊丟掉。
- **越界寫入是原版行為**：邊界清除迴圈跑 320 圈而畫面只有 200 列、blit 超出 malloc 區、`memmove` 比來源多讀一筆記錄。
- **`Icon%02d.dat` 用章節索引而 function 名稱用章號**，兩者差 1，三十支章節 init 全部會踩。

最後一項還帶出一個獨立的坑：那三十支處理函式看起來一模一樣，**但不能用樣板生成**——除了檔名差 1，`fdps_roster_add_character` 必須排在 `fdps_chapter_state_reset` 之前（reset 會依名冊人數重建地圖單位，順序反過來新加入的角色會被歸零成退場），而且第 17／22／23 章傳的游標目標不是 0。

## 邊界問題只回報不處理

5 個判定回報了邊界異常，全部是同一種形狀：function 的 body 裡沒有 `RET`，控制流以 `JMP` 落進鄰居的 body（`0003bd99`、`0003be36`、`0003c4ff`、`0003c6bc`、`0003c7aa`，全在 CD 那一段）。那是 Watcom 把兩支近乎相同的 C function 的收尾段合併掉的結果，不是邊界設錯，票 14.2 已經逐一確認過並刻意保留。

落地腳本只計數與回報，不動邊界。這一點在寫腳本的時候就決定了：命名這一趟不該改變程式的結構，而「reader 覺得這裡怪」與「這裡真的錯了」是兩件事，前者的正確處置是留下記錄。

## 數字

| | |
| --- | --- |
| 判定的 function | 417（另有 97 個由票 12 定案，只覆核未重判） |
| workflow 呼叫 | 5 次（1 次試驗 8 個、3 次主批、1 次續跑回掃） |
| agent | 609，錯誤 17（全部是 session 上限那一輪） |
| 回掃重讀 | 133 |
| 收尾時仍未決 | 0 個低信心、0 個推測 convention、0 個 `needs_rescan` |
| 記錄的重建陷阱 | 320 |
| 單一 agent 的 context | 62k–176k token，與處理總量無關 |
| 每輪 gate | 孤立程式碼 0、error bookmark 0、未定義 byte 0、命名違規 0，全程未破 |
