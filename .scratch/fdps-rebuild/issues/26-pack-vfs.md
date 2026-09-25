# 26 — 光碟 `PACK.VFS` 內封存的另一版遊戲

**What to build:** 知道光碟 `PACK.VFS` 裡封存的那一版遊戲是什麼、比正式版早還是晚、和正式版差在哪裡，並把它獨有或不同的圖片、音效、音樂、文字 dump 出來整理進知識庫。

**已知事實：**

- `PACK.VFS` 兩片光碟都有（約 82 MB），24 個成員名稱與順序相同（清單見 `resource_info/disc_images.md`）。遊戲執行時只從裡面讀 `Pass.Dat`（判斷目前是第幾片），其餘成員程式完全不讀。
- 成員與安裝版逐 byte 比對：
  - 相同：`BACKGRND.VFS`、`DOS4GW.EXE`、`FIELD1.VFS`、`FIGACT.VFS`、`FIGHT.VFS`、`FMER1/2.TMP`、`MER1/2.TMP`
  - 安裝版有對應檔但內容不同：`FDE.EXE`（對應 `FDPS.EXE`，372,789 對 373,301 byte，兩片光碟上的 `FDE.EXE` 也彼此不同）、`FIELD.VFS`（兩片也不同）、`FIELD2.VFS`、`ICONANI.VFS`（兩片也不同）、`MISC.VFS`、`FDE.SAV`（兩片也不同；光碟 1 的與 `F30.SAV` 相同）、`DIG.INI`（Sound Blaster、IRQ 7）、`DISK.NO`（`CD_ROM at E:`，安裝程式寫的是 `CDROM at e:`）
  - 只在 `PACK.VFS`：`DATA.VFS`（九張數值表另外包成一個容器）、`DEBUG.SAV`、`F30.SAV`
- `FDE.EXE` 的字串與 `FDPS.EXE` 幾乎相同，但它引用 `Data.vfs`，正式版沒有；正式版的數值表在 `MISC.VFS` 裡。
- 看起來像某台開發機工作目錄的快照（執行期產生的混色快取、三個存檔、單機的音效設定）。
- **版本先後未定。** 傾向較舊的間接跡象：`PACK.VFS` 的 `ICONANI.VFS` 有地圖 31、32 各自專用的過場腳本（`ICON31.DAT`、`ICON32.DAT`）與一支 4 byte 的 `ICON34.DAT`，正式版把序章合併進地圖 32，地圖 31 成了孤兒。反面可能：它是與正式版平行的另一個建置。
- 一次調查的解包結果在 `workspace/cut_content/investigation/cut_chapter/pack/`（`disc1`、`disc2`），差異的初步觀察：`ICON00`、`ICON01`、`WIN00` 內容不同，`WIN07`、`WIN08`、`WIN16` 結尾多一個切換地圖指令，光碟 2 的 `WIN29`、`WINGA26` 不同；光碟 2 的 `FIELD.VFS` 幾乎所有 `FDETXT` 與字模檔都不同，神秘商店提示直白得多；`ATTR110.DAT`、數個 `MISC` 的 SAF、`ITEM.DAT`、`MAGICDAT.DAT` 不同。以上都未驗證。

**待決（開工時先與開發者確認）：**

- 分類名稱：版本先後證明之前不預設立場，候選「封存版」；決定後寫進 `CONTEXT.md`
- 兩份 `FDE.EXE` 要不要匯入 Ghidra 成為獨立的 program 做 function 比對（`FDPS.LE` 不動）——這是判斷版本先後與找出被拿掉的程式功能（`DEBUG.SAV` 暗示的除錯模式）的唯一硬證據
- 知識庫的位置：候選 `cut_content/pack/`

**Blocked by:** 25.1（舊版字模檔也要用同一方法解）

**Status:** 延到下一次

- [ ] 每個成員與正式版的差異逐一確認並記錄
- [ ] 版本先後有結論，或寫明為什麼無法斷定
- [ ] 獨有或不同的圖片存 PNG、音效存 WAV、文字全文照錄（舊版字模檔的對照表照 25.1 的方法補齊）
- [ ] `resource_info/disc_images.md` 連到新文件
