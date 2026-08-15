# 12 — Call graph 建立與骨幹走查

**What to build:** 對這個程式有了整體圖像。call graph 建出來讓叢集結構可見，並且從 entry point 到主迴圈的骨幹路徑被人工走過一遍且命名完成。這是開始平行處理個別 function 的兩個前置條件——沒有 call graph 就只能按位址亂切，沒有走過骨幹就缺乏共同的整體認識。

**Blocked by:** 11

**Status:** done

- [x] 完整 call graph 建出，叢集結構可辨識
- [x] entry point 到主迴圈的骨幹路徑逐一走過並語意命名
- [x] 主要子系統的入口點識別出來（初始化、主迴圈、戰鬥、選單、繪圖、輸入、檔案存取、音效）
- [x] 共用輔助程式碼的叢集標示出來，供後續分區時整包放在同一區
- [x] 整體架構的概觀進知識庫

## 結果

- Call graph：1,042 個 function、3,167 條直接邊、411 條表分派邊。從進入點可達 790，不可達 252 分成 150 個弱連通分量。指標表偵測順帶更正了 `memory_layout.md` 三處記載（80x87 表實為四段、6 張未記載的跳躍表、17 段基底在執行期算出的指標表）。
- 骨幹走查逐一定案 109 個 function，全部有語意名稱、plate comment 與 prototype。信心 high 61、medium 47、low 1。八個子系統的進入點全部識別出來。
- 頂層迴圈解出來了：`main` 每圈跑一次戰鬥玩家階段，再依請求碼 `00069da0` 分派（0 續戰、1 全滅回標題、2 章節通關走劇本表 `00060304` 再進村莊）。
- 75 個共用 helper 與 5 個遞迴環打上 function tag，分成五組寫進知識庫供後續分區使用。
- 三輪落地全部 gate 乾淨：孤立程式碼 0、error bookmark 0。

結論寫在 [`program_info/architecture.md`](../../../program_info/architecture.md)，工具是 `tools/call_graph/` 與 `tools/backbone_walk/`。

走查前緣尚餘 77 個位址未走（預算上限停止），其中影響後續的項目列在 `architecture.md` 的「尚未確定的事」；13 個 blit 模式處理常式與兩個 AI 階段入口是最大的兩塊，屬於票 15 的範圍。
