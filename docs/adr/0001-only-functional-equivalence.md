# 只要求功能等價，不追求 binary 一致

重建出來的執行檔只需要在外顯行為與功能上與原版 `FDPS.EXE` 一致；DOS bind stub 大小、LE object 佈局、function 排列順序、暫存器配置等 binary 層級的差異一律不要求對齊。唯一的例外是「某個 binary 層級的差異會實際影響功能或執行正確性」——例如時序敏感的熱迴圈長度改變導致與音效的隱性時序失衡——那才另外要求對齊。

## Considered Options

前作 FD2 專案的 README 宣稱「100% 復刻」，容易讓人以為目標是 byte-exact。實際上 FD2 的等價定義也只到「外顯行為一致 + 功能等價」，byte-exact 明確不追求，因為暫存器配置、function 排列、alignment padding 都由 compiler 與 linker 決定，強求既不切實際也無必要。本專案沿用同樣的標準，並且講得更明白。

## Consequences

`hash_check` / `eqcheck` 這類 binary 比對工具的角色因此改變：它們比對的是「本專案上一版 build 與這一版 build」，作為防止意外改動的自我回歸閘，而不是拿來跟原版比對。

另外，這個決定讓沿用前作抽出的 AIL 靜態庫成為可行選項（見 ADR-0004）——那份庫做不出 byte-identical 的結果，但在功能等價的標準下這不構成問題。
