# 建置旗標與預設 calling convention

`FDPS.LE` 是用哪一套工具鏈、哪一組編譯與連結旗標產生的。本檔擁有旗標組本身與每一項的判定依據；位址空間的分段與邊界由 [`program_info/memory_layout.md`](../program_info/memory_layout.md) 擁有，本檔只引用。

判定的是**整份 binary 的預設值**。個別 function 的 calling convention 有例外，處理方式見本檔最後一節。

## 工具鏈

**Watcom C/C++ 10.0a**（10.0b 無法排除，兩者的相關產物 byte 相同）。四項獨立證據都指向 10.0 家族的後期版本，並排除 9.5 全系列與 10.5 之後：

| 證據 | 內容 |
| --- | --- |
| CRT 的版權字串 | `0x3fa49`：`WATCOM C/C++32 Run-Time system. (c) Copyright by WATCOM International Corp. 1988-1994.` 年份上界 1994 就是 10.0 家族 |
| 隨遊戲附的 `DOS4GW.EXE` | 265,420 byte，SHA-256 與 10.0a／10.0b 的 `BIN\DOS4GW.EXE` **完全相同**。9.5c 大小相同但雜湊不同，10.5 之後是 265,396 byte |
| `FDPS.EXE` 的 MZ stub | 10,832 byte。用 10.0a 的 `wlink system dos4g` 連結任意程式，產出的 stub 與 `FDPS.EXE` 的前 10,832 byte **零 byte 差異** |
| CRT 函式的機械碼 | `0x435f3` 的轉大寫常式（31 byte 無重定位）與 `0x43657` 的 `__CHP`（22 byte）只在 10.0／10.0a／10.0b 的 `CLIB3S.LIB` 命中，9.5 全系列與 10.5 之後都沒有 |

`0x42c3a` 的 `FCOS`／`FSIN` 包裝常式命中 `MATH387x.LIB`（非 `MATH3x`），`0x4ec3c` 起的 80x87 模擬器命中 `EMU387.LIB`。

## 編譯旗標

```
wcc386 -bt=dos4g -mf -4s -fpi -s -ot -od
```

**`-ot -od` 的順序不能對調，也不能只留一個。** `wcc386` 由左而右處理選項：`-ot` 先設定「以速度為優先」這個偏好，`-od` 再關掉最佳化器但不會清掉那個偏好。三種寫法的結果都不同：

| 寫法 | 區域變數 | 索引縮放 |
| --- | --- | --- |
| `-od` | 來回堆疊 | `shl reg,2` |
| `-ot` | 留在暫存器 | `lea reg,[reg*4]` |
| `-od -ot` | 留在暫存器 | `lea reg,[reg*4]` |
| **`-ot -od`** | **來回堆疊** | **`lea reg,[reg*4]`** |

原版是「來回堆疊 + `lea`」，只有最後一種同時給出這兩項。`-otd` 是等價的縮寫。四種寫法的框架形狀相同，所以框架不是分辨這一組的依據。

每一項的判定依據如下。「原版的樣子」欄位是 `FDPS.LE` 的實際觀察，「另一種選擇會變成」是用同一支編譯器實測其他旗標的產出。

| 旗標 | 原版的樣子 | 另一種選擇會變成 |
| --- | --- | --- |
| `-bt=dos4g` | CRT 走 `__x386_init` 這條 DOS/4G 啟動路徑，執行檔是 LE 容器配 DOS/4G stub。容器格式其實由 wlink 的 `system` 決定，這個旗標決定的是預定義巨集與 header 搜尋路徑 | 換成別的 build target 會拉到另一套 header 與啟動碼 |
| `-mf`（flat） | 具名 const 物件與區域陣列的初值影像放在 object 1（`0x146d2`、`0x2b27a`、`0x31037` 等夾在遊戲函式之間）；字串字面值與浮點常數放在 object 2 的 `CONST`；複製初值到堆疊前**不重載 ES** | `-ms` 會把 const 一起放進 DGROUP，且每次複製前多兩條 `mov ax,ss` / `mov es,ax` |
| `-4s`（486，堆疊呼叫慣例） | 序幕固定 `53 56 57 55 89 e5`（推 EBX/ESI/EDI/EBP 後建 EBP 框架），第一個引數在 `[ebp+0x14]`；收尾用 `mov esp,ebp` / `pop ebp`，全 binary 遊戲段 **0 個 `LEAVE`**；16-bit 載入保留 `MOVSX`（208 處） | `-3s` 收尾用 `LEAVE`；`-5s` 把每個 `movsx eax,word ptr X` 換成 `mov eax,dword ptr X-2` + `sar eax,0x10`（遊戲段 0 處）；`-4r`／`-3r` 等 register 慣例不會無條件推四個暫存器 |
| `-fpi`（內嵌 x87，含模擬） | 遊戲段有 70 條內嵌 x87 指令，且映像檔內含 `EMU387.LIB` 的 80x87 模擬器 | `-fpc` 完全不產 x87，改呼叫 `__I4FD`／`__FDM` 等；`-fpi87` 只差在不發出 `__init_387_emulator` 這個外部參照，**實測即使在 `.lnk` 明列 `emu387.lib`，`-fpi87` 產出的映像檔裡也沒有模擬器**——wlink 只抽出解得掉未定義符號的 lib 成員 |
| `-s`（移除堆疊檢查） | 遊戲段 414 個標準框架的 function **沒有任何一個**呼叫 `__CHK`（`0x4361a`）；17 個呼叫端全部是序幕就是 `push imm` / `call __CHK` 的程式庫 function | 不加 `-s` 時每個有框架的 function 都會被插入 `push <框架大小>` / `call __CHK` |
| `-ot`（以速度為優先） | 位址計算的索引縮放編成 `lea reg,[reg*N + 0]`：遊戲段 304 處，程式庫段 41 處。`shl reg,2` 只出現在除法常數展開之類的算術情境（37 處） | 不加 `-ot` 時位址縮放也用 `shl reg,N`；`-os`（以空間為優先）同樣是 `shl` |
| `-od`（關閉最佳化） | 每個區域變數都寫回堆疊再讀出；switch 的跳躍表放在序幕之後、以 `jmp short` 跳過，分派拆成 `mov` + 縮放 + `jmp cs:[reg+表]` 兩三條指令 | 開最佳化後區域變數留在暫存器、跳躍表移到函式之前，分派收斂成單一條 `jmp cs:[reg*4+表]`——`FDPS.LE` 裡程式庫段的六張表正是這個形狀，遊戲段那張不是 |

`-zq` 只影響訊息輸出，可加可不加。

### 無法從 binary 判定的旗標

- **`-fp2` / `-fp3` / `-fp5` / `-fpr`**：在這組旗標下四者與不指定產生完全相同的機械碼，本 binary 沒有可分辨的痕跡。
- **`-od` vs `-d2`**：`-d2` 會連帶關閉最佳化，接在 `-ot` 之後給出與 `-od` 相同的機械碼；LE header 的 `debug_info_off` 是 0，表示最終執行檔沒有除錯資訊。取 `-od`。
- **`-zp`（結構對齊）**：編譯器預設等同 `-zp1`（實測 `struct {char a; int b; char c; short d; double e;}` 在預設下的欄位偏移是 0/1/5/6/8）。要確認原版是否另外指定，得等 struct layout 定案，屬於票 17。

### 逐指令對得上的一段

`0x2f6d0` 那個 8-case switch 是整組旗標的收斂點，用上表的旗標重編一份等價的 C，出來的指令序列與位元組填充完全一致：

```
FDPS.LE 0x2f6d0                          wcc386 -mf -4s -fpi -s -ot -od
  push ebx / esi / edi / ebp               push ebx / esi / edi / ebp
  mov  ebp,esp                             mov  ebp,esp
  sub  esp,0x10                            sub  esp,<n>
  jmp  short（跳過表）                     jmp  short L2
  8b c0（兩 byte 填充，對齊表）            mov  eax,eax
  <8 筆跳躍表>                             L1 DD ...×8
  cmp  dword ptr [ebp+0x34],7              cmp  dword ptr +14H[ebp],7
  ja   <default>                           ja   near ptr L11
  mov  eax,dword ptr [ebp+0x34]            mov  eax,dword ptr +14H[ebp]
  lea  eax,[eax*4 + 0]                     lea  eax,+0H[eax*4]
  jmp  dword ptr cs:[eax + 0x2f6e0]        jmp  dword ptr cs:L1[eax]
```

## 連結指令

```
system dos4g
name FDE.EXE
option stack=8k
file fde.obj
file <其餘 .obj>
library clib3s.lib
library math387s.lib
library emu387.lib
```

| 項目 | 依據 |
| --- | --- |
| `system dos4g` | LE header 的 CPU type 2／OS type 1、三個 `BIG32` object、DOS/4G stub。這個 system 定義**不會自動帶任何 C runtime**，三個 lib 必須自己列 |
| `option stack=8k` | DGROUP 的 `STACK` 段是 8,192 byte（範圍見 [`memory_layout.md`](../program_info/memory_layout.md)），初始 ESP 指向段尾。wlink 的**預設是 4K**，實測不寫這行只會拿到 `0x1000` |
| 連結輸出叫 `FDE.EXE`，含 `main` 的模組是 `fde.obj` | LE 的 resident name table 是 `fde`。實測 wlink 把這欄填成輸出檔的主檔名，而沒有 `name` 指令時輸出檔名又取自第一個 `.obj`。所以這一個觀察無法分辨「明寫了 `name FDE.EXE`」與「沒寫 `name`、第一個 obj 叫 `fde.obj`」——但兩條路都會產生相同的 header，重建時擇一即可。無論哪一條，`FDPS.EXE` 都是事後改名 |
| 三個 lib | 見上節的 byte 比對。`clib3s`／`math387s` 的 `s` 後綴就是堆疊呼叫慣例的版本，這是 `-4s` 在連結層的獨立佐證 |
| 沒有 debug directive | `debug_info_off` = 0 |
| stub 用預設的 `wstub.exe` | stub byte 與 10.0a 產出的完全相同 |

object 3（`0x70000`，84 byte）不是上面任何一段產生的——它是某個 vendor 模組自帶的、不屬於 DGROUP 也不屬於 CGROUP 的資料段（取用範圍見 [`memory_layout.md`](../program_info/memory_layout.md)）。它掛在哪個 lib 上屬於票 14／19。

## 個別 function 的 calling convention

**預設是堆疊慣例（`-4s`），但不能假設全域統一。** 每個 function 的 cc 必須在 emit 時於程式碼中明確宣告，不靠旗標帶過；逐一判定屬於票 15。

現況的量測（1,042 個 function）：

- 468 個是標準的四推序幕（其中 414 個在 `0x3b000` 以下的遊戲段）——堆疊慣例
- 18 個序幕就是 `push imm` / `call`——沒有 `-s` 的程式庫模組，同樣是堆疊慣例。其中 17 個呼叫 `__CHK`，第 18 個（`0x51f6b`）呼叫的是別的東西
- 其餘 556 個是手寫組語或開了最佳化的 vendor 程式碼，形狀各異
- 全 binary 只有 2 個 function 含 `RET imm`（`0x4361a` 的 `__CHK`、`0x5038a`）

辨識偏離預設的 function，訊號強度由強到弱：

1. **呼叫端在 `CALL` 之後沒有 `ADD ESP,n`，但有引數被傳進去**——引數不在堆疊上
2. **callee 以 `RET n` 結束**——callee 自己清引數，不是預設慣例
3. **呼叫端在 `CALL` 之前設定 EAX／EDX／EBX／ECX**，且 callee 在序幕之後立刻讀這幾個暫存器
4. **callee 在 entry 就把 EBX 當輸入讀而沒有先 `PUSH`**——堆疊慣例下 EBX 是 callee-saved，這是強烈的 register 慣例訊號
5. 序幕不是四推、或引數不在 `[ebp+0x14]` 起——至少不是遊戲模組的預設形狀

判斷時要**跳過 `push imm` / `call __CHK` 這兩條**，它們是堆疊檢查不是 cc 訊號；`__CHK` 刻意保留 EAX／EDX／ECX／EBX，讓 register 引數能安然通過。含 varargs 的 function 一律是堆疊慣例。

## 重現方式

[`tools/build_flags/`](../tools/build_flags/_index.md) 收了全部判定腳本。所有 Watcom 工具都在 DOSBox-X 裡以 DOS 版執行，與原版的建置環境一致。`verify_flags.py` 會用上表的旗標組編譯探針並逐項比對本檔列出的 15 個特徵，旗標組若被改動就重跑它。
