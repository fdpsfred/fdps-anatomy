# libs — 重建要連進去的第三方程式庫

不是本專案的產物，也不是從 `FDPS.LE` 抽出來的。這裡放的是重建版連結時要用、而工具鏈本身不附帶的東西。

| 子資料夾 | 內容 |
| --- | --- |
| `ailv3/` | Miles AIL v3 音訊函式庫 `ailv3.lib` 與標頭 `ailv3.h`。前作 FD2 從 `FD2.LE` 抽出並重建的產物，逐 byte 沿用 |

Watcom 自己的 `clib3s.lib`／`math387s.lib`／`emu387.lib` 不放這裡——它們來自 Watcom 10.0a 的安裝，由建置腳本以全路徑引用。

## `ailv3/`

沿用而不從 `FDPS.LE` 重抽的理由見 [ADR-0004](../docs/adr/0004-reuse-fd2-ail-library.md)；怎麼連、遊戲要自己提供什麼見 [`rebuild_info/ail_link.md`](../rebuild_info/ail_link.md)。

`ailv3.h` 是前作 `tools/ail_extract/gen_ailv3_h.py` 的產物，不手改。它每個宣告都掛 `#pragma aux ... "*" modify [eax ebx ecx edx]`：`"*"` 讓符號名不被修飾以對上程式庫的 PUBDEF，`modify` 列出 vendor object 的暫存器破壞集合——少了它編譯器會把活值留在 EBX 跨過 AIL 呼叫，被無聲吃掉（[`rebuild_info/pitfalls.md`](../rebuild_info/pitfalls.md)）。
