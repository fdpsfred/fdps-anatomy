# rebuild_info — 重建視角

回答「怎麼把 `FDPS.LE` 重建成功能等價的執行檔，以及重建時哪裡會踩雷」。`program_info/` 回答遊戲現在做什麼，本資料夾回答重建它需要知道什麼。

涵蓋範圍：工具鏈與建置流程、vendor library 的 ABI 契約、pool 判定規則、等價性鐵則與回歸閘的判定規則、emit 一支 function 的流程與檢查項目、`src/` 的檔案編排與命名慣例、以及踩過的坑的最終結論。

等價的定義是功能等價而非 byte-exact，見 [ADR-0001](../docs/adr/0001-only-functional-equivalence.md)。

| 文件 | 內容 |
| --- | --- |
| [`build_flags.md`](build_flags.md) | 工具鏈版本、`wcc386` 與 `wlink` 的旗標組與逐項判定依據、`-od` 之下 inline 展開的指紋與辨識法、預設 calling convention 與偏離者的辨識法 |
| [`build_pipeline.md`](build_pipeline.md) | 自動化建置流程：DOS 版工具的落點、掛載配置、旗標的送入方式、三訊號結束偵測、前置檢查，以及最小驗證程式證明了什麼 |
| [`build_gate.md`](build_gate.md) | 自我回歸閘：通過的條件、五種等價判定的意義、基準值的推進規則與更新流程、閘門看不到的東西 |
| [`emit_pipeline.md`](emit_pipeline.md) | 一個 function 怎麼變成 C：五個角色的分工與序列的理由、emit 的 gate、calling convention 的宣告寫法、八類隱性契約檢查表、測試的組織方式、工作狀態與續跑、中斷復原的時機與界線、批次大小歸誰決定 |
| [`code_layout.md`](code_layout.md) | `src/` 怎麼切：分組依據、每檔的行數預算、資料符號歸誰、標頭與 `extern` 的擁有者、不 emit 的四類符號、超標的處置規則、76 個檔各自負責什麼 |
| [`ail_link.md`](ail_link.md) | Miles AIL 靜態庫的連結契約：庫向外要的符號、遊戲必須自己提供的七個、兩邊名字不同時的 alias 接法、執行期要有的檔案，以及已經驗證到什麼程度 |
| [`naming.md`](naming.md) | 符號前綴、pool 分類、8.3 檔名限制、「Ghidra 名稱與 C 名稱逐字相同」這條鐵則，以及 function 內部區域變數與參數的命名規則與它的兩道關卡 |
| [`pitfalls.md`](pitfalls.md) | 「照直覺寫就會與原版不同」的事項總表：不能修的原版 bug、不能加的檢查、不能換的型別與寫法、不能照字面理解的資料、不能照編譯器慣例設定的旗標，以及 vendor 程式庫的 ABI 與涵蓋範圍 |

## 踩雷點要當下就記

任何工作段落——不分是在解資源格式、讀 assembly、查編譯器行為還是跑實機——只要發現一件「重建時照現代直覺或編譯器慣例寫就會偏離原版」的事，當下就要寫進 [`pitfalls.md`](pitfalls.md)，不能等到重建階段再回頭翻。理由是這種發現幾乎都是別的工作的副產品：解格式的人看到 `strcmp` 沒有轉大寫只是順手一瞥，真正需要它的人是三十個票之後才動手寫那支讀取器的人，中間沒有任何機制會把這件事送到他面前。

收錄門檻是**照直覺寫就會錯**，不是「這裡很複雜」。格式細節與數值不在 `pitfalls.md` 重複，它只寫「會錯在哪、照直覺會怎麼寫」並連回擁有該事實的正典文件。
