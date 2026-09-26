# docs/

這個資料夾放兩種東西：專案自己的文件，以及一個要對外發佈的網頁。

| 路徑 | 內容 |
| --- | --- |
| `adr/` | 決策記錄 |
| `agents/` | agent 的工作規範 |
| `research/` | 研究筆記（含前作 FD2 的 playbook） |
| `guide/` | 攻略站的原文鏡像，只供字串搜尋（[`tools/guide_scrape/`](../tools/guide_scrape/_index.md)） |
| `character-stat-comparison/index.html` | 角色屬性數值比較網頁，由 [`tools/growth_table/`](../tools/growth_table/_index.md) 的 `build_page.py` 產生，不要手改；要改就改 `tools/growth_table/page_template.html` 或產生器，再重跑建置 |
| `.nojekyll` | 讓 GitHub Pages 不經 Jekyll、原樣送出檔案 |

## 發佈角色屬性比較網頁

網頁是單一個自足的 HTML 檔（資料、CSS、JS 全部內嵌，沒有任何外部引用），放到任何靜態主機都能用。啟用 GitHub Pages 是開發者的動作，這裡只列做法；發佈網址由開發者決定後補在這裡。

### 注意：以 `/docs` 為來源會公開整個資料夾

GitHub Pages 的「Deploy from a branch」選 `main` 的 `/docs` 時，發佈的是**整個 `docs/`**，不只是網頁：決策記錄、agent 規範、研究筆記，以及**攻略站的全文鏡像 `docs/guide/`**（別人的著作，只為了在專案內搜尋而鏡像）都會變成公開網址。不打算公開這些，就不要選這個來源。

### 可選的做法

1. **另開只放網頁的 `gh-pages` 分支（建議）。** 用 `git subtree` 把網頁資料夾切成一個獨立分支，分支的根就是 `index.html`：

   ```
   git subtree split --prefix docs/character-stat-comparison -b gh-pages
   git push origin gh-pages
   ```

   `Settings → Pages → Deploy from a branch` 選 `gh-pages` 分支的 `/ (root)`，網址是 `https://<帳號>.github.io/<repo>/`。網頁更新後重跑這兩行（分支已存在時先 `git branch -D gh-pages`，推送時加 `--force`）。`character-stat-comparison/.nojekyll` 會跟著進分支。
2. **GitHub Actions 只上傳網頁資料夾。** 用 `actions/upload-pages-artifact` 指定 `path: docs/character-stat-comparison`、再 `actions/deploy-pages`，`Settings → Pages` 的來源選「GitHub Actions」。公開的也只有網頁，不必維護另一個分支。
3. **以 `main` 的 `/docs` 為來源。** 最省事，網址是 `https://<帳號>.github.io/<repo>/character-stat-comparison/`，但會連帶公開上面列的全部內容。

網頁不含遊戲的圖像、音效或文字腳本，只有從資料表推導的數值，以及角色、職業、法術與徽章的名稱。
