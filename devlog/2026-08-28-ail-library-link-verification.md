# 把前作的 AIL 靜態庫真的連起來（票 19）

ADR-0004 決定沿用前作 FD2 從 `FD2.LE` 抽出的 `ailv3.lib`，理由是一整套比對：版本字串同源、110 條 debug 字串表位元組相同、428 個 function 逐一比對 394 個 body 相同、再用 Function ID 從另一條路量到一樣的結論。全部都是比對。沒有人把它連起來過。

這一票就是把它連起來。

## 票寫錯了一件事，而錯的那件事是最花時間的那一項

票 19 的驗收清單裡有一條：「補上前作的庫沒有的那 12 個 `ail` function（主體是 `0003c984`–`0003d176` 的 LX 驅動映像載入層），否則連結會留下未解析符號」。這條看起來是整票的主體——那批合計快 4 KB，其中 `0003ccf8` 一支就 1,151 byte，要手寫成 C 是一整輪逐 function 的工作。

先去問庫本身要什麼，而不是先動手補。`wlib -x` 把 `ailv3.lib` 拆成 `ail_code.obj` 與 `ail_data.obj`，掃它們的 EXTDEF 記錄，庫向外要的東西一共兩類：二十幾個 Watcom CRT 符號，以及七個遊戲側符號（六支 DPMI 服務常式加一支存 EFLAGS 的）。**那 12 個一個都沒出現在 EXTDEF 裡。** 也對——庫是替 FD2 打包的，它不可能參照 FDPS 才有的東西。

「連結會留下未解析符號」這個預期是從「庫裡沒有這些 function」推出來的，中間少了一步：沒有人參照的東西不會變成未解析符號。真正會變成未解析符號的是反方向的那七個，票裡沒提。

先量再補，省下的不是一點時間。

## 那批 LX 載入層在原版裡也沒人呼叫

EXTDEF 只說明「連結不缺」，不說明「功能不缺」。分開問了第二個問題：那批東西在 `FDPS.LE` 裡有沒有可達的呼叫端？

`0003ccf8`、`0003cbc1`、`0003c9db`、`0003c9eb` 的 xref 是空的。`0003c984` 有 17 個呼叫端，票 14.1 的 devlog 記過這個數字，當時的結論是「同一塊裡另外八個是活的」——但那 17 個全部來自 `0003cbc1` 與 `0003ccf8`，也就是來自那三個沒人呼叫的 function 內部。整塊是封閉的：連結器把 Miles 的某個 object 整包抽了進來，程式從來沒走進去過。

「有呼叫端」與「可達」是兩件不同的事，而票 14.1 量的是前者。

順便把互補的那一面也量了：遊戲程式碼實際呼叫的 AIL 進入點有 18 個，逐一對 `ailv3.lib` 的符號表查，18 個全在。

## `-silent` 會把 Sound Blaster 一起關掉

連結第一次就過，未解析符號 0 個。執行第一次沒過：`AIL_startup` 成功，`AIL_install_DIG_INI` 回 NULL，而 `AIL_get_last_error_code` 回 **0**。

錯誤碼 0 是最難查的形式——看起來像什麼都沒發生。先懷疑 IRQ：DOSBox-X 預設 SB16 在 IRQ 7，前作的腳本用 IRQ 5，而且它的註解特別寫「IRQ 不一致會無聲地讓 PCM 播不出來」。照前作的參數（IRQ 5、`mixer=true`、`BLASTER` 補上 `P330 T6`）重跑，一樣。

前作同一支腳本裡還有一行註解：`-silent` 會連 SB 硬體模擬的回應一起關掉。拿掉 `-silent`，一次就全過了。回頭把 IRQ 換回 7 再跑，也過——唯一的變因就是 `-silent`。

CLAUDE.md 寫著「要用 silent mode 執行以達成全自動化」，這裡是那條規則的第一個例外。代價比想像中小：不加 `-silent` 會開一個視窗，但程序照樣自己跑完自己退出，三訊號偵測完全不受影響。

## 名字對不上，接點放在連結器

庫的 EXTDEF 寫的是 `fd2_dpmi_lock_size`，FDPS 這邊叫 `fdps_dpmi_lock_size`。`naming.md` 有一條鐵則：Ghidra 名稱與 C 名稱逐字相同。兩側都不能改名。

wlink 的 `alias` 指令正好是為這種事存在的，七條寫進 `.lnk` 就接上了。這比在 `src/` 裡用 `#pragma aux` 把符號改名好，因為那會讓讀 `src/` 的人看到一個 Ghidra 裡不存在的名字。

第七條的兩邊差得更遠：庫要的是 `crt_equivalent_get_eflags_thunk`。前作把那 4 個 byte 判成 CRT 的等價實作，還帶著一個 5-byte thunk；FDPS 判定它是 AIL 自己的。thunk 本身是廠商 `.LIB` 把同一支 helper 收兩份時的連結產物，沒有原始碼可寫，所以 alias 直接落在本體上。

那 4 個 byte 是 `PUSHFD` / `POP EAX` / `CLI` / `RET`，寫不成 C。它成了專案第一個 `.asm`，`wasm` 在 10.0a 的落點是 `BINB\`，跟 `wcc386` 同一個目錄。

六支 DPMI 常式是這一票唯一的 emit 工作，六支的 assembly 各自讀過一次才寫成 C，沒有從其中一支推另一支——`fdps_dpmi_unlock_region` 是跳進 `fdps_dpmi_lock_region` 的中段共用尾巴的，只看其中一支不會知道這件事。六支的量沒有另外開 workflow，是直接一支一支處理的；下一次遇到成打的 function 就要照 ADR-0007 寫 workflow。

## 客戶端從「裝得起來」擴到「走得到 ISR」

「`AIL_install_DIG_INI` 回非 NULL」是很容易停下來的地方，但它證明的只有 INI 被解析了。第一版就往下走到真的配一個 sample handle、灌一段 11,025 Hz 的方波、輪詢 `AIL_sample_status` 直到它從 `PLAYING` 變成 `DONE`——這條路走過 DMA 緩衝區與混音器。

查完「遊戲用到 18 個進入點」之後又補了一輪：`AIL_register_timer` 註冊一個 callback、設頻率、起跑、確認 callback 真的被觸發。計時器 ISR 是遊戲側那六支 DPMI 鎖頁常式唯一的理由，API 裡沒有別的東西會走到它，不測等於那六支只驗證到「連結解得掉」。

`AIL_install_MDI_INI` 保留下來當觀察項而不是驗收項：FDPS 沒有 `MDI.INI` 也沒有任何 `.MDI` 驅動程式，音樂走 CD 音源，所以它在原版安裝上就會回 NULL。這裡如果過了，反而表示測試環境不是出貨的那一個。

## 一個永遠不會失敗的檢查

「未解析符號 0 個」是整票的主要結論，而 `parse_undefined` 從頭到尾沒有抓到過任何東西——它到底會不會抓，沒有證據。加了 `selftest`：拿掉一條 alias 重連，要求連結真的失敗、而且要求輸出裡點名那個符號。跑起來兩項都成立，最後再用完整的 alias 重建一次，不把壞掉的執行檔留在樹上。

這跟票 18 的 `selftest` 是同一個道理：掛住與跑完在成功路徑上長得一樣，只有負面案例分得出來。

## 覆核提出的 memset，查下去是原版沒有

程式碼覆核指出那三支 `int386` 包裝沒有把 `union REGS` 清零，未指定的成員會帶著堆疊殘值進 DPMI 呼叫，而前作 FD2 的同一組 function 在 `src/util/dpmi.c` 裡每次呼叫前都 `memset`。

前作有，不代表原版有。回去看 `0003ca49` 的 assembly：`PUSH ESI` / `SUB ESP,0x38` 之後直接 `MOV dword ptr [ESP],0x100` 與 `MOV dword ptr [ESP+0x4],EAX`，然後就呼叫下去了——0x38 byte 的框架裡其餘的位置一個都沒動。`0003cb01` 與 `0003cad2` 也一樣。原版沒有清零。

用到的四個 DPMI function 都不讀那些帶殘值的暫存器（`0100` 只讀 BX、`0101` 只讀 DX、`0600`／`0601` 讀 BX:CX 與 SI:DI），所以補上 `memset` 不會改變行為，只會在 function 裡多出原版沒有的程式碼。留原樣，把「原版就是這樣、前作那份不能照抄」寫進註解。

同一次覆核提的另一件事採納了：那三支的序幕確實是 probe-free 的（`PUSH ESI` / `SUB ESP`，沒有 `PUSH n` / `CALL __CHK`），而它們跑在 AIL 的 driver setup 與中斷路徑上。旗標組釘死了 `-s`，所以今天加不加 `#pragma off (check_stack)` 產出的執行檔位元組完全相同（119,341 byte，加之前加之後一樣）——加它是為了讓沒有 `-s` 的建置變體不會無聲地在那條路徑上插進堆疊探測。
