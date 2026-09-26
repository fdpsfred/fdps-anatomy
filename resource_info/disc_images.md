# 光碟映像內容

驗證對象：`FDPS_DISC_1.cue` / `.bin`、`FDPS_DISC_2.cue` / `.bin`。兩片皆為 MODE1/2352 資料軌加多條紅皮書音軌的混合模式光碟。

## 兩片光碟的分工

光碟 1 是安裝來源，資料軌帶著完整的可安裝遊戲檔案與 `INSTALL.BAT`。光碟 2 只有執行期需要的檔案，外加一個遊戲不讀的填充檔 `PACK1.VFS`。

`INSTALL.BAT` 把 `DOS4GW.EXE`、`FDPS.EXE`、`*.CEL`、`FIELD*.VFS`、`BACKGRND.VFS`、`FIGHT.VFS`、`FIGACT.VFS`、`ICONANI.VFS`、`MISC.VFS`、`*.DIG`、`SETSOUND.EXE`、`AILDRVR.LST` 複製到硬碟的 `\FDPS`，寫出 `Disk.No`（內容為 `CDROM at <磁碟機>:`），最後執行 `SETSOUND` 做音效卡設定。

因此執行期仍留在光碟上、不會被複製到硬碟的是：`PACK.VFS`、`FD.EXE`、三組 `.VID` / `.AUD`，以及全部音軌。

## 各檔案的角色

- **`PACK.VFS`**（兩片都有，內容不同）：VFS 容器，header 帶簽章字串 `Dynasty Information Co.,`，內含 24 個 entry。兩片的 entry 名稱與順序完全相同，依序為 `BACKGRND.VFS`、`DATA.VFS`、`DEBUG.SAV`、`DIG.INI`、`DISK.NO`、`DOS4GW.EXE`、`F30.SAV`、`FACE.CEL`、`FDE.EXE`、`FDE.SAV`、`FIELD.VFS`、`FIELD1.VFS`、`FIELD2.VFS`、`FIGACT.VFS`、`FIGHT.VFS`、`FMER1.TMP`、`FMER2.TMP`、`ICON.CEL`、`ICONANI.VFS`、`MER1.TMP`、`MER2.TMP`、`MISC.VFS`、`PASS.DAT`、`SBLASTER.DIG`——注意這份清單與資料軌上的檔案清單不是同一組，`FDE.EXE`、`FDE.SAV`、`F30.SAV`、`DEBUG.SAV`、`DATA.VFS`、`DIG.INI`、`DISK.NO`、`FMER*.TMP`、`MER*.TMP`、`PASS.DAT` 只存在於容器內。

  兩片之間有 5 個成員內容不同，其餘 19 個成員位元組相同：

  - `PASS.DAT`：3 個 byte，光碟 1 為 `1\r\n`、光碟 2 為 `2\r\n`，是遊戲判斷目前放的是哪一片的依據。
  - `FDE.EXE`（372,789）與 `FDE.SAV`（22,987）：兩片大小相同而內容不同，只憑大小分辨不出來。
  - `FIELD.VFS`（249,543 / 248,869）與 `ICONANI.VFS`（4,718,665 / 4,718,685）：大小差 −674 與 +20，恰好構成容器總大小 654 byte 的落差。
- **`PACK1.VFS`**（只有光碟 2）：139 MB 的填充檔，外形是一個 ARJ 封存——主標頭名為 `XXX.ARJ`、唯一成員 `ARCHIVE.RAR` 以不壓縮方式存放 139,465,923 byte、檔尾是 ARJ 結束標記——但成員內容是無法壓縮的高熵位元組，CRC32 與標頭記載的不符，也沒有 RAR 簽章，不是 VFS 容器，執行檔中沒有任何參照。
- **`FD.EXE`**：以 Watcom C/C++32 編譯、掛 DOS/4GW stub 的 32-bit LE 執行檔（LE header 在 `0x2a50`），過場動畫播放器。由 `FDPS.LE` 的 `fdps_play_movie`（`0x30f40`）以 `spawnlp(P_WAIT, …)` 呼叫，兩片上的檔案位元組相同。
- **`FD1.VID` / `FD1.AUD`、`FD2.VID` / `FD2.AUD`、`END.VID` / `END.AUD`**：三段過場動畫的影像與伴音，交給 `FD.EXE` 播放。兩片上的檔案位元組相同。
- **音軌**：遊戲音樂，由 `FDPS.LE` 直接下 MSCDEX 命令播放。

程式端如何使用這些檔案見 `program_info/cd_audio.md`。

## 內容清單

### DISC 1

映像 `FDPS_DISC_1.bin`，227,025 個 raw sector，資料軌 32 個檔案，音軌 21 條（總長 25 分 56 秒）。

| 檔名 | 大小 | SHA-256 |
| --- | ---: | --- |
| ADRV688.DIG | 11,278 | `294a7de77d88a7f38f58b32fc7166ced87a4c44e6c9be760f78b1724744d7161` |
| AILDRVR.LST | 16,131 | `76b6d7d77c015bba4a810d699e06af77ea25f6c52a08a7b0eb588ea33fea6018` |
| BACKGRND.VFS | 1,421,083 | `933364b885b2f82f793abe62dc4d191ddada1df7f188a0ab6b450b0fdf7d3e13` |
| DOS4GW.EXE | 265,420 | `dd9f4f342533f99570475b62e53231a468de2e3d83e4fc31e80c27d3a7d6b49c` |
| END.AUD | 948,933 | `6c5c117fa46b8317ee711ccfffb86a74e599e60c81680fefe80109134c6142c6` |
| END.VID | 19,175,529 | `a3cf568536b76aaa768212ca0a5d71700963c6e6b02dfc9ca3d7242400b67c28` |
| FACE.CEL | 679,928 | `af472935ec8a1f51ca7281ad4761614671150755a0cc72b6fdfb71fbfffc5fcd` |
| FD.EXE | 125,183 | `723e6d9be364657646b0c92074dfe8704775b4b3e50be9f46ac92191cd60a3b2` |
| FD1.AUD | 641,699 | `c712c3c1c2309fbd55c7dce4f53f9461ccbce4c6d58c4bbae7e63c39345c8269` |
| FD1.VID | 26,483,861 | `0dd282fe2cfd56bc2451cd731c78665538312f7159adbb8b2073764e2c44b7e2` |
| FD2.AUD | 457,945 | `36d19634cd58f5d2a2f1d006f4b5510515eac4970d94d9ae191696dfe2f68bdd` |
| FD2.VID | 11,985,587 | `683404b8f1eba1a596ad76a859d67d16949b89c4cacc3b2d179032bf276d8939` |
| FDPS.EXE | 373,301 | `22f93aefce021d5baded7e44b4187f4bee53930d84fa0840b77dee071b805c55` |
| FIELD.VFS | 249,545 | `bc2b02e7aea399c46868a00abcb7a2a63fe4b8b8442393fe3f4416206f233d3d` |
| FIELD1.VFS | 8,934,513 | `48346de5ea4d96749c0b6a424bfc57db5d5f29d2850c966cacfccd09c0f0e5d5` |
| FIELD2.VFS | 112,350 | `2d86bc5df12df92ed58cdf65c750d09a11b9e91e2b27268bbcea897bc7ea9cae` |
| FIGACT.VFS | 22,632,235 | `b90e79cc75d16f84f824de7a541ca8c785a27fc8e886060a72b03420be18e18c` |
| FIGHT.VFS | 15,136,893 | `8f5f4547a2670d393ee05abb311b5b3b3320d7d5ee0e70b150c7228db61f70ce` |
| ICON.CEL | 630,141 | `e81576d624db3b56ac2e4c6e5ba787c320eb7c4151c21ca1c197043744545e41` |
| ICONANI.VFS | 4,718,103 | `8628d6298a41aa04309d9ec01d1c17b6031dae8bc267d04edc5cfbf789840c46` |
| INSTALL.BAT | 409 | `bda8637875706bcf334278887cfc06cc7b3e14487fdf479d943488fe3d8c4e88` |
| JAMMER.DIG | 3,219 | `cca38c28eabe00b88e4bddd627fa68a68b2559029098b777129ca00201831a9c` |
| MISC.VFS | 27,398,923 | `f7514e11e88893903801ddd6c5e8ab9c9915a8508f41ec00937f36254829fd28` |
| PACK.VFS | 82,658,897 | `5dbfe9d4213df5976efc7c8fa414f7a4cebfe459a66094d537809995d6500c5b` |
| PROAUDIO.DIG | 2,127 | `63cc6ba6c0bebf80ebb073cfbef2b0de1ce508d006a94180095a0223ac4bb33c` |
| RAP10.DIG | 2,849 | `d4e03dd5313e02291df43d41be2d289f7006a30ed24a195d8e872518999566bf` |
| SB16.DIG | 2,812 | `865fab2e1318f341848dd12db578e881ecb45264e794ed39d47adc235071580e` |
| SBLASTER.DIG | 2,984 | `3e55f51b14d98cf9db92f334ebf96585a55e725816c590e7eac99fc387da79d2` |
| SBPRO.DIG | 2,763 | `250c3afdb960a8b3c6fb268e7e610db4a2a43f626c0e1ddee7ff952150a94dae` |
| SETSOUND.EXE | 167,953 | `c3b5f3102eba94bef138cb92f36b77926b017471af17d47b7d4ad61e9b446fd5` |
| SNDSCAPE.DIG | 4,171 | `a1459763f2fdcb1ada4f9fc6d28c9ea33077c00dad91b5809ee899f5a1b6c05c` |
| ULTRA.DIG | 9,478 | `4e63a94e272f37907910cc2a73120d2d21f3651a66bd06fcac7e80c93af55478` |

| 音軌 | 起始 LBA | 長度（秒） |
| ---: | ---: | ---: |
| 2 | 110,332 | 202.01 |
| 3 | 125,483 | 37.87 |
| 4 | 128,323 | 74.52 |
| 5 | 133,912 | 77.57 |
| 6 | 139,730 | 76.04 |
| 7 | 145,433 | 89.31 |
| 8 | 152,131 | 58.96 |
| 9 | 156,553 | 66.89 |
| 10 | 161,570 | 88.25 |
| 11 | 168,189 | 70.56 |
| 12 | 173,481 | 73.45 |
| 13 | 178,990 | 41.93 |
| 14 | 182,135 | 69.48 |
| 15 | 187,346 | 52.25 |
| 16 | 191,265 | 69.95 |
| 17 | 196,511 | 44.32 |
| 18 | 199,835 | 34.67 |
| 19 | 202,435 | 114.44 |
| 20 | 211,018 | 75.71 |
| 21 | 216,696 | 65.07 |
| 22 | 221,576 | 72.65 |

### DISC 2

映像 `FDPS_DISC_2.bin`，280,282 個 raw sector，資料軌 9 個檔案，音軌 15 條（總長 31 分 37 秒）。

| 檔名 | 大小 | SHA-256 |
| --- | ---: | --- |
| END.AUD | 948,933 | `6c5c117fa46b8317ee711ccfffb86a74e599e60c81680fefe80109134c6142c6` |
| END.VID | 19,175,529 | `a3cf568536b76aaa768212ca0a5d71700963c6e6b02dfc9ca3d7242400b67c28` |
| FD.EXE | 125,183 | `723e6d9be364657646b0c92074dfe8704775b4b3e50be9f46ac92191cd60a3b2` |
| FD1.AUD | 641,699 | `c712c3c1c2309fbd55c7dce4f53f9461ccbce4c6d58c4bbae7e63c39345c8269` |
| FD1.VID | 26,483,861 | `0dd282fe2cfd56bc2451cd731c78665538312f7159adbb8b2073764e2c44b7e2` |
| FD2.AUD | 457,945 | `36d19634cd58f5d2a2f1d006f4b5510515eac4970d94d9ae191696dfe2f68bdd` |
| FD2.VID | 11,985,587 | `683404b8f1eba1a596ad76a859d67d16949b89c4cacc3b2d179032bf276d8939` |
| PACK.VFS | 82,658,243 | `213189b99f8bac2d9197a3fee0e0382d9a39c8623b11796abbf9c778c93dcb05` |
| PACK1.VFS | 139,466,029 | `65112a769658f784c3b195f9a103d701e2b5cea1cb5426e22c2571717cbd3dba` |

| 音軌 | 起始 LBA | 長度（秒） |
| ---: | ---: | ---: |
| 2 | 137,998 | 199.99 |
| 3 | 152,997 | 35.84 |
| 4 | 155,685 | 86.23 |
| 5 | 162,152 | 91.11 |
| 6 | 168,985 | 67.45 |
| 7 | 174,044 | 32.64 |
| 8 | 176,492 | 112.41 |
| 9 | 184,923 | 142.88 |
| 10 | 195,639 | 95.19 |
| 11 | 202,778 | 89.27 |
| 12 | 209,473 | 157.27 |
| 13 | 221,268 | 124.41 |
| 14 | 230,599 | 87.95 |
| 15 | 237,195 | 288.16 |
| 16 | 258,807 | 286.33 |
