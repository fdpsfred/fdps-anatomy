# 重建踩雷點

「照直覺寫就會與原版不同」的事項總表。每一條都是原版做了一件不合常理、而重建時很容易順手改掉的事。

本檔擁有的是「這件事在重建時會出錯」這個判斷與對策；事實本身由連結指向的正典文件擁有，這裡不重複佈局與數值。收錄門檻是**照著現代直覺或編譯器慣例寫就會偏離原版**，單純「這裡很複雜」不收。

分成三類：

- **不能修的原版 bug**：原版寫錯了，但外顯行為依賴它，或至少不能無聲地改掉。功能等價的定義見 [ADR-0001](../docs/adr/0001-only-functional-equivalence.md)。
- **不能加的檢查**：原版沒有做的驗證，補上去會讓原本能跑的輸入被擋掉。
- **不能換的型別與寫法**：語意上「等價」但實際行為不同的替換。

## 不能修的原版 bug

| 事項 | 照直覺會怎麼寫 | 正典 |
| --- | --- | --- |
| `.SAF` 的 magic 檢查是 `p[0]=='S' \|\| p[1]=='A' \|\| p[2]=='F'`，三個條件是 OR | 寫成 `&&` 或 `memcmp`。改了之後原本放行的檔會被擋下 | [`resource_info/saf.md`](../resource_info/saf.md) |
| `.CEL`／`.SAF` 的繪製器逐列扣 column 數，一個 op 超出列尾會讓計數繞回成極大值並寫穿記憶體 | 加上 clamp 或提早 break。原版是靠編碼端保證每列剛好填滿，繪製器本身不設防 | [`resource_info/cel.md`](../resource_info/cel.md) |

## 不能加的檢查

| 事項 | 照直覺會怎麼寫 | 正典 |
| --- | --- | --- |
| `.VFS` 開啟時完全不驗證 magic、版本與簽章 | 開檔先 `memcmp` magic。原版餵一個非 VFS 檔進去不會被擋下 | [`resource_info/vfs.md`](../resource_info/vfs.md) |
| `.CEL` 的偏移表位置寫死 `+0x0F`，header 的 `0x05` 欄位從不讀 | 改成讀 `0x05` 當表位置。兩者目前恰好都是 15，改了在畸形檔上行為就不同 | [`resource_info/cel.md`](../resource_info/cel.md) |
| `.CEL` 的像素格式欄位 `0x0D` 從不讀，全程只有一條解碼路徑 | 依 `0x0D` 分派兩種解碼器。原版會把 `M310.CEL` 當 4-op RLE 讀，這個矛盾未收斂，見 [`open_issues.md`](../open_issues.md) | [`resource_info/cel.md`](../resource_info/cel.md) |

## 不能換的型別與寫法

| 事項 | 照直覺會怎麼寫 | 正典 |
| --- | --- | --- |
| `.VFS` 成員查找是**單向**轉大寫：把傳入的名稱就地轉大寫，entry 名稱原樣取用，兩者 `strcmp` | 寫成 `stricmp(entry, query)`。遇到非全大寫的 entry 名稱行為就不同，而且原版會就地改寫呼叫端的緩衝區，這個副作用是可見的 | [`resource_info/vfs.md`](../resource_info/vfs.md) |
| `.VFS` 的 entry 筆數以 8-bit 讀入，第 256 筆以後走不到；entry table 偏移以帶號 16-bit seek，上限 `0x7FFF` | 用 `u32` 讀筆數、用 `long` seek。容器沒有踩到上限，但這是原版的硬限制 | [`resource_info/vfs.md`](../resource_info/vfs.md) |
| `.SAF` 的 tilemap 格子編號是 `i16`（`short *` 取值、`-1 < index` 擋下界），但 layer 的 tilemap 編號是零延伸的 `u16` | 兩個都寫成同一種索引型別 | [`resource_info/saf.md`](../resource_info/saf.md) |
| `.SAF` 的 layer 半透明程度以 16-bit `MOVSX` 讀 `+0x07`，連 `+0x08` 的保留 byte 一起讀進來 | 宣告成 `u8`。保留 byte 恆為 0，所以目前無差別，但欄位的實際寬度是 2 | [`resource_info/saf.md`](../resource_info/saf.md) |
| 章節音軌表的位元組要 **+1** 才是 MSCDEX 音軌編號，加法由呼叫端在起播前做，不在表裡 | 直接把表值當音軌編號送出去，整首曲子會差一軌 | [`program_info/cd_audio.md`](../program_info/cd_audio.md) |
| CD 命令的 `INT 2Fh` 不是指令，是 DPMI `INT 31h` AX=0300h 的 real-mode call structure 裡的資料位元組 | 直接寫 `int 0x2f` 內嵌組語。在 DOS/4G 保護模式下走不通 | [`program_info/cd_audio.md`](../program_info/cd_audio.md) |

## 環境與範圍

| 事項 | 內容 | 正典 |
| --- | --- | --- |
| 啟動的三道光碟檢查 | `access("DISK.NO")`、由 `Disk.no` 第三個 token 取得路徑前綴、MSCDEX 安裝檢查，任一不過就 `exit(1)`。重建版跑起來前這三件都要滿足 | [`program_info/cd_audio.md`](../program_info/cd_audio.md) |
| 影片播放不在重建範圍 | 三段過場由光碟上的 `FD.EXE` 播放，`FDPS.LE` 只負責 `spawnv` | [`program_info/cd_audio.md`](../program_info/cd_audio.md) |
| CD 音源在重建範圍內 | 選曲、起播、停止、循環全部由 `FDPS.LE` 自己下 MSCDEX 命令 | [`program_info/cd_audio.md`](../program_info/cd_audio.md) |
