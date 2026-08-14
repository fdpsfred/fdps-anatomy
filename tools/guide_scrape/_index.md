# guide_scrape — 攻略站鏡像

把青衫攻略站的內容頁抓下來存成原文鏡像，並提供對鏡像做字串搜尋的入口。鏡像本身在 [`docs/guide/`](../../docs/guide/_index.md)。

不解析、不產出結構化資料，理由見 [ADR-0006](../../docs/adr/0006-guide-as-mirrored-text-not-parsed-data.md)。

## `guide_scrape.py`

四個子命令，全部以遊戲代號參數化（`fdps` 外傳、`fd2` 二代、`fd` 一代）：

```
python tools/guide_scrape/guide_scrape.py fetch  <game>
python tools/guide_scrape/guide_scrape.py list   <game>
python tools/guide_scrape/guide_scrape.py show   <game> <page>
python tools/guide_scrape/guide_scrape.py search <pattern> [--game <game>] [-C N] [-i]
```

`fetch` 依遊戲代號組出 URL（`.../c31/{game}/{game}/`，`{game}` 出現兩次是站台本身的目錄結構），把每頁寫成 `docs/guide/<game>/` 底下的 `.htm` 與同名 `.txt` 兩份，並一併抓下頁面引用的所有圖片（攻略頁每章的地圖）。圖片放同一層，所以原始 HTML 的相對 `src` 在本地打開時仍然指得到。

頁面清單寫在腳本內的 `GAMES` 表，一頁一列 `(本地檔名, 站台頁名, 頁面標題)`。三代的頁面組成不同——一代沒有法術列表、多一頁存檔修改，二代沒有密技頁——且攻略頁在站台上的檔名是遊戲代號本身（`fdps.htm`），本地一律存成 `walkthrough`，所以本地檔名與站台頁名不是同一個東西。

`search` 對純文字鏡像做正規表示式搜尋，預設搜全部三代，印出命中行與上下文行號。查資料的標準動作是先 `search` 再讀上下文。`list` 列出鏡了哪些頁，缺的標 `(missing)`。`show` 印出單一頁的純文字，頁名對 `GAMES` 表驗證。

## 純文字版怎麼產生的

內容全在單一 `<pre>` 區塊內，靠等寬字型的顯示寬度對齊欄位，所以剝標籤時**不能**替換成任何字元：`<b>` 之類就地刪掉，後面的字才會停在原位。區塊標籤（`<table>`、`<td>`、`<p>`⋯）同樣就地刪掉而不是換成換行——換成換行會憑空多出空行，作者自己用來分隔區段的多重空行就分辨不出來了。只有兩個例外：`<br>` 是換行，`<img>` 換成 `[圖 檔名]`，指向同層的鏡像圖檔。`<head>` 整段丟掉，否則 `<title>` 會變成第一行、和頁面自己的標題重複。

頁面有 UTF-8 BOM，解碼時去掉，並額外清掉殘留在內文中的 BOM；換行一律正規化成 LF。

## 報錯而不是產出殘缺鏡像

HTTP 非 200、回應為空、頁面不含 `<pre>` 區塊、`<img>` 的 `src` 讀不出來或指向本目錄以外，都直接中止並印出出錯的 URL。

抓取全部完成才落地：所有頁面與圖片先收在記憶體，任何一項失敗就整批不寫。邊抓邊寫會在中途失敗時留下一半新一半舊的鏡像，那正是這個工具要防的狀態。
