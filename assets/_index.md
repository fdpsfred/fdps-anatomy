# assets — 資料表視角

回答「遊戲的數值內容是什麼」。一個檔對應一組遊戲資料（物品、法術、人物、職業、成長曲線等），並是該組資料的唯一正典。

與另外兩個視角的分工：`program_info/` 說明程式怎麼使用這些數值，`resource_info/` 說明承載它們的檔案格式長怎樣，本資料夾只寫數值本身。

每份文件開頭有「來源」段，寫明每個欄位的依據——攻略站（鏡像在 [`docs/guide/`](../docs/guide/_index.md)）、資料檔實際 byte、或 Ghidra 中的 global 符號。三者互相牴觸時以資料檔實際 byte 為準，並在文件中記下差異。

[`tables/`](tables/_index.md) 放 record 的 struct 定義，數值本身不重複寫在那裡，一律引用本層的正典檔。

本層的表另有一個查詢入口：`fdps-data` skill 可依名稱、代碼與數值範圍反查這些數值，資料集由 [`tools/data_skill/`](../tools/data_skill/_index.md) 從 `MISC.VFS` 現解並逐列對照本層的表產生。

| 文件 | 內容 |
| --- | --- |
| [`items.md`](items.md) | 226 個物品的類型、AP／HIT／DP／EV、附加屬性、距離、價格與使用效果 |
| [`spells.md`](spells.md) | 40 個法術的威力、命中率、距離、範圍、MP 與作用對象 |
| [`characters.md`](characters.md) | 十二名我方人物的出場基礎值、升級成長範圍與法術習得等級，以及三者合成實際數值的公式 |
| [`classes.md`](classes.md) | 40 個職業代碼的地形行動力消耗、暴擊率與魔法抗性 |
