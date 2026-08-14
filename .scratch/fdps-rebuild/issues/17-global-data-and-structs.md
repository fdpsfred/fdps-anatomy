# 17 — Global data 命名與 struct 定義

**What to build:** decompiled C 讀起來是有意義的欄位存取，而不是滿篇的位移運算。全域資料符號有語意名稱，主要的資料結構有定義好的佈局。

**Blocked by:** 15

**Status:** ready-for-agent

- [ ] 全域資料符號逐一判定用途並語意命名
- [ ] 主要 struct 的佈局定義出來並套用（角色執行期結構、物品、法術、職業、章節等）
- [ ] struct 欄位語意與攻略基準真值交叉驗證
- [ ] 存檔結構與前作的對應關係確認並記錄
- [ ] 資料段與未初始化段的分界確認
- [ ] Ghidra 快照匯出並 commit
