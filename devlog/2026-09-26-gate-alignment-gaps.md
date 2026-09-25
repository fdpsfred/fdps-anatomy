# 回歸閘學會跳過 `wcc386` 不清零的對齊空隙（票 27）

## 起點

票 25.15 在 `game` 目標撞上 `different`，查出來全是對齊填充，用 `pad_diff.py` 判讀後推進了基準值。票面要的是根本解：閘門自己把空隙抹掉再比，而且要嚴格——只抹證明得出來的空隙，並證明閘門仍抓得到真正的改動。

先查前作 `fd2-anatomy`：`tools/` 下的 `fd2_build`、`fd2_diff` 與知識庫都沒有處理過對齊空隙，搜 `padding`／`pad` 只找到資料表格式與 Ghidra 的孤立程式碼掃描。前作的閘門根本沒走到「同一份原始碼在兩個工作目錄建出不同映像」這一步。所以只有抹零的手法（`lefixup.py` 抹 fixup site 與整張 record table）可以照抄，空隙的定位得自己做。

## 先把兩份映像擺在手上

開了一個 `HEAD` 的 worktree（`workspace/gate_pad/wt`，`fdps_game_files` 用 junction 接過去），`build_game.py build` 只要 34 秒。主工作目錄的 `FDE.EXE` 雜湊就是現在的基準值，與 worktree 的建置差 270 byte，`pad_diff.py` 說 268 byte 是字面值填充、2 byte 在 `data_fdps_ui_play_active_flag` 之後——與 25.15 的觀察相符（那次是 272，當時主工作目錄還混著別的 LF 檔）。

## 死路一：只看內容判斷「NUL 後補到 4 byte」

`pad_diff.py` 的字面值規則是「兩邊在前三個 byte 內有共同的 NUL，且本 byte 在下一個 4 byte 邊界之前」。要放進閘門，就得改成對單一映像成立的規則，第一版是：`CONST` 裡每個重定位目標是一個字面值起點，從它找第一個 NUL，NUL 之後到 4 byte 邊界是空隙。

拿真的 `FDE.EXE` 掃 `CONST` 的 331 個重定位目標，逐個檢查「起點對齊、下一個目標正好在邊界上」，冒出 9 個怪東西：

- `0x2a8` 是 `00 00 00 00 00 00 f8 3f`——`double` 1.5，第一個 byte 就是 NUL。
- `0xe60`、`0xe68` 是兩個 `66 66 66 66 66 66 f2 3f`（1.15）。
- `0x1090` 起的 `.\`、`.bat`、`.com`、`COMSPEC`……是 CRT 的字串，**不對齊**，一個接一個擠在一起。

去翻目的檔：`AISCORE.OBJ` 的 `CONST` 只有那個 1.5，`UNIT.OBJ` 是兩個 1.15（沒合併）。也就是說 `wcc386` 把浮點常數也放在 `CONST`，不是 `CONST2`。純看內容的規則會把某些 `double`——低位 byte 可印、NUL 落在後半——的指數 byte 當空隙抹掉。機率很低，但這正是票面說的「不能用猜的」。

## 改成看「誰怎麼用它」

統計每個 `CONST` 目標的引用點前兩個 byte：絕大多數是 `xx B8`（`mov r32,imm32` 的 opcode 在 site 前一個 byte）、少數 `68`（`push imm32`），`double` 全是 `DC 0D`（`fmul qword ptr [disp32]`），CRT 的 `inf`／`nan` 是 `8B 15`（`mov edx,[disp32]`）。於是字面值的證明改成：**每一個**引用點都把目標的位址當值用——位在資料物件裡（存起來的指標），或 code 裡 site 前一個 byte 是 `68` 或 `B8`–`BF`。x87 記憶體運算元 site 前面是 ModRM，不可能是這幾個值。證明不了的照常比對。

證明不了的共 8 個：3 個 `double`、CRT 的 4 個，外加 `0xdcc` 的 `"+"`——它被 `66 A1`（`mov ax,[m]`）整個讀成一個 word，是 inline 複製短字面值。考慮過把「讀取寬度沒蓋到空隙」也算證明（要解 `66`、`A1`、`8B` 等讀取寬度），但只為了這一個而且它的空隙在所有實驗裡都沒變過，決定不做，留在比對範圍內。

另外加了兩道：下一個起點必須**正好**落在 NUL 之後的 4 byte 邊界（CRT 那種緊密排列就過不了），NUL 之前不能夾著具名符號或證明不了的目標。

## 死路二：只靠 map 找 `_DATA` 的符號

`FDE.MAP` 只列 public。`data_fdps_ui_play_active_flag` 在 `+0x1549`，map 上的下一個符號在 `+0x1550`，差 6 byte，大於對齊單位，嚴格的規則不會抹。倒回去看 `GAMEDATA.OBJ` 的記錄，`+0x3c` 有一筆 `LPUBDEF`：`indicator_queue_run_align_below`——票 23 為了重現原版佈局放的檔案範圍 `static`。map 不列 static，目的檔列。所以閘門得讀目的檔：用同模組 public 在 map 裡的位址減它在目的檔裡的位移，得出模組基底，再放 static。前作「別自己寫 OMF parser、用 `wlib`」的結論是針對 `.lib` 的怪記錄；這裡只讀本專案自己編出來的目的檔的 `LNAMES`／`SEGDEF`／`PUBDEF`／`LPUBDEF`，自己讀。

也看過能不能直接從目的檔認出哪些 byte 是「沒寫的」：`MAIN.OBJ` 的 `CONST` 分四筆 `LEDATA`，記錄內部的空隙是殘值（`"Fight.pal\0"` 後面接著 `at`、`"Cusor.cel\0"` 後面接 `Up`），只有記錄結尾的空隙不在任何 `LEDATA` 裡。殘值與真資料在目的檔裡長得一樣，這條路不通。

## 符號大小從哪來

三個候選：

1. `tools/data_emit/data/manifest.json` 的 `size`——那是原版映像的大小，不是 `src/` 實際宣告的大小；兩者不一致正是閘門該抓的東西，不能拿它當依據。
2. 產生一個 `sizeof` 探針檔在 DOSBox 裡編——精確，但每次閘門多一輪編譯，而且 static 取不到。
3. 讀被編譯的那份原始碼（建置時暫存的 `stage/SRC`），找符號唯一的檔案範圍定義，只認純量、指標與整數字面值維度的陣列。

選 3。`src/` 55 個 `_DATA` 符號裡 9 個取不到大小（巨集維度的陣列、函式指標表），全是 4 的倍數大小、後面沒有空隙，影響不到現在的判定；取不到就照常比對。

算出來的遊戲映像空隙：字面值 258 段、符號 3 段（`ui_battle_animation_enabled`、`ui_play_active_flag` 之後各 2 byte，`MAPCUR.OBJ` 那 2 byte 的 `_DATA` 之後是連結器補到下一個模組的 2 byte），共 520 byte。worktree 與主工作目錄兩份建置的 270 個不同 byte 全部落在裡面，兩邊空隙位置相同。

## 基準值只存指紋

基準值只有雜湊，沒有映像，所以不能「拿兩邊的空隙聯集抹零」。做法是每一邊用自己的 map 算自己的空隙、抹零後算殘差雜湊，另外把空隙位置本身也雜湊進指紋；`pad` 要兩者都相等。考慮過把基準映像存進版控——`FDE.EXE` 含 AIL 程式庫的內容，而且 36 萬 byte 的二進位每推進一次就多一份，不做。空隙位置進指紋的好處在合成測試裡看得到：把 `unsigned char` 改成 `unsigned short`、新值的高位 byte 剛好是 0 時，兩邊抹零後的殘差**相等**，只有空隙位置不同才把它判成 `different`（變異測試：拿掉位置比對，這一列就紅）。

## 合成的建置與變異測試

`lepad.py selftest` 自己組一份 LE 映像（code 與 data 兩個物件、真實格式的 fixup record）、wlink 格式的 map、一個 OMF 目的檔與一份原始碼，擺好字面值、會被誤認的 `double`、只有目的檔知道的 `static`、不知大小的 struct。全部綠燈後逐一把關鍵條件拿掉看哪一列變紅：拿掉 x87 的證明 → 6 列紅；拿掉「下一個起點正好在邊界」→ 1 列紅；拿掉對齊單位的上限 → 起初**沒有任何一列紅**，因為沒有目的檔時根本不會有已知大小的符號，那一列測的不是它；補了一列「目的檔少了 static，`data_mode` 之後 6 byte 不是空隙」才抓到。拿掉檔案範圍檢查 → 1 列紅。只有「空隙裡不能有重定位位置」這道拿掉後沒有一列紅——在前面的起點規則成立時它到不了，保留為防禦並在程式裡寫明。

## 實建：什麼會讓殘值改變

在乾淨 worktree 逐項改一處、重建、用閘門的比對對上沒改的建置（`workspace/gate_pad/experiment.py`）：

| 改動 | 判定 | 不同的 byte |
| --- | --- | --- |
| `main.c` 加一行註解 | `identical` | 0 |
| 每個 `.c` 開頭加一行註解 | `identical` | 0 |
| 函式本體裡加一行註解 | `identical` | 0 |
| 改寫既有註解的文字 | `identical` | 0 |
| `main.c` 的巨集改名（值不變） | `identical` | 0 |
| `gamedata.h` 開頭加一行、加 700 字元的註解 | `identical` | 0 |
| `main.c` 改成 LF（含改註解） | `identical` | 0 |
| 全部 `.c` 改成 LF | `identical` | 0 |
| 全部 `.h` 改成 LF | `pad` | 305 |
| 只把 `fdpstype.h` 改成 LF | `pad` | 6 |
| 只把 `gamedata.h` 改成 LF | `identical` | 0 |
| 全部 `.c`／`.h` 改成 LF | `pad` | 305 |
| `data_fdps_ui_play_active_flag` 初值 1 → 2 | `different` | 1 |
| `"Fight.pal"` → `"Fight.pbl"` | `different` | 1 |
| `exit(1)` → `exit(2)`（一個 code byte） | `different` | 1 |

票面與 25.15 的 devlog 都寫「只改註解就會變」，這裡推翻了：七種註解與巨集名的改法全部逐 byte 相同，觸發的是**標頭檔**的換行字元。25.15 那次「把改動複製進 worktree 只差 20 byte」，複製過去的檔帶著主工作目錄的 LF，才是那 20 byte 的來源。知識庫照實測改寫。

中途一次寫 worktree 的 `main.c` 得到 `PermissionError`（上一輪建置剛結束，檔案短暫被鎖），重跑同一項就過了，worktree 沒有殘留改動。

## `ailsmoke` 與 `pad_diff.py`

`ailsmoke` 原本不寫 map，連結檔加了 `option map`（`AILSMOK.MAP`）。它編到的 `src/dpmi.c` 沒有任何資料段內容，空隙全來自凍結的 `ailsmoke.c` 的字面值，所以實務上幾乎不會觸發，但規則一致。`smoke` 不編 `src/`、沒有 map，維持原樣。

`pad_diff.py` 原本自帶一條較寬的字面值規則，會與閘門的判定分歧。改寫成吃兩個儲存庫根目錄、用 `lepad.py` 算兩邊的空隙，把每個不同的 byte 分成重定位／空隙／無法解釋，無法解釋的列出前後 map 符號——閘門報 `different` 時用它找原因。
