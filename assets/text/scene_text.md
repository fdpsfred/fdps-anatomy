# 額外場景的文字：`FDETXT31`–`FDETXT65`

地圖編號 30 以上的場景各有一個文字區塊 `FDETXT(地圖編號 + 1).TXT`，共 35 個。它們不屬於任何一章，只在過場腳本以 `SWITCH_MAP` 切到該地圖時隨整組地圖資源載入，腳本的 `DRAW_TEXT` 與 `ASK_THREE_WAY` 從中取條目（[`resource_info/cutscene_script.md`](../../resource_info/cutscene_script.md)）。本頁的原文由 [`tools/global_text/`](../../tools/global_text/_index.md) 以 [`text_decode`](../../tools/text_decode/_index.md) 解出，每條由哪支腳本顯示取自 [`cutscene_script`](../../tools/cutscene_script/_index.md) 的追蹤。

## 只有腳本會顯示這些區塊

章節索引（也就是決定目前文字區塊的那個值）只有過場腳本的 `SWITCH_MAP` 會寫成 30 以上。腳本以外的寫入者——章節結束處理函式寫入的下一章（最大 `0x1D`）、標題畫面的新遊戲與示範戰鬥、讀檔——寫的都是 0–29。勝利腳本結束在過場地圖上時，結束處理函式接著只寫入下一章，不畫字；之後城鎮階段（`fdps_load_field_chapter_resources`）或下一章的開場（`fdps_chapter_state_reset`）照新的章節索引重新載入文字區塊。存讀檔畫面的章名則另外以存檔格記錄的章節載入。腳本執行中從目前區塊取字的只有 `DRAW_TEXT` 與 `ASK_THREE_WAY`，所以本頁區塊的一條條目會不會顯示，完全由有沒有腳本在該地圖上引用它決定。

由此：

- 章節區塊開頭放章名、勝敗條件與城鎮招牌的前 9 條，在這裡只是普通條目：有腳本引用就會顯示（例如 `FDETXT36` 的 `0x04`），沒有就不會。
- 沒有腳本引用的條目與沒有任何腳本切過去的區塊不在本頁轉錄：它們是永遠不會顯示的文字，由 [`cut_content/`](../../cut_content/_index.md) 收錄。
- `{speaker char=n}` 換上肖像編號 n 的頭像與新的對話框、`{speaker unit=n}` 換上目前單位陣列第 n 格的頭像，都不顯示名字；角色是誰見 [`assets/`](../_index.md)。轉錄時 `{br}` 換成換行，`{speaker …}` 之前也換行。

## 總表

| 區塊 | 地圖 | 條數 | 切過去的腳本 | 顯示的條目 |
| --- | ---: | ---: | --- | --- |
| `FDETXT31` | 30 | 9 | （無） | — |
| `FDETXT32` | 31 | 19 | （無） | — |
| `FDETXT33` | 32 | 19 | `ICON00` | `0x09`–`0x12` |
| `FDETXT34` | 33 | 19 | （無） | — |
| `FDETXT35` | 34 | 21 | `ICON00` | `0x09`–`0x14` |
| `FDETXT36` | 35 | 12 | `ICON00` | `0x04`–`0x06`, `0x09`–`0x0b` |
| `FDETXT37` | 36 | 12 | `WIN00` | `0x09`–`0x0b` |
| `FDETXT38` | 37 | 13 | `ICON09`、`WIN09` | `0x09`–`0x0c` |
| `FDETXT39` | 38 | 14 | `ICON09` | `0x09`–`0x0d` |
| `FDETXT40` | 39 | 11 | `WIN03` | `0x09`–`0x0a` |
| `FDETXT41` | 40 | 23 | `ICON11` | `0x0a`–`0x15` |
| `FDETXT42` | 41 | 23 | `WIN00` | — |
| `FDETXT43` | 42 | 23 | `WIN00` | — |
| `FDETXT44` | 43 | 12 | `WIN00` | `0x09`–`0x0b` |
| `FDETXT45` | 44 | 10 | `WIN18` | `0x09` |
| `FDETXT46` | 45 | 11 | `WIN17`、`WIN17-1` | `0x09`–`0x0a` |
| `FDETXT47` | 46 | 11 | `WIN05` | `0x09`–`0x0a` |
| `FDETXT48` | 47 | 13 | `ICON06`、`WIN24` | `0x09`–`0x0c` |
| `FDETXT49` | 48 | 12 | `ICON07` | `0x09`–`0x0b` |
| `FDETXT50` | 49 | 14 | （無） | — |
| `FDETXT51` | 50 | 15 | `WIN22` | `0x09`–`0x0e` |
| `FDETXT52` | 51 | 20 | `WIN20`、`WIN24` | `0x0b`–`0x13` |
| `FDETXT53` | 52 | 10 | `ICON11` | `0x09` |
| `FDETXT54` | 53 | 11 | `WIN11` | `0x09`–`0x0a` |
| `FDETXT55` | 54 | 13 | `ICON08` | `0x09`–`0x0c` |
| `FDETXT56` | 55 | 14 | `ICON08` | `0x09`–`0x0c` |
| `FDETXT57` | 56 | 10 | `ICON24` | `0x09` |
| `FDETXT58` | 57 | 12 | `WIN08` | `0x09`–`0x0b` |
| `FDETXT59` | 58 | 18 | `ICON19` | `0x09`–`0x0d` |
| `FDETXT60` | 59 | 17 | `WIN24` | `0x09`–`0x10` |
| `FDETXT61` | 60 | 13 | `WIN16` | `0x09`–`0x0c` |
| `FDETXT62` | 61 | 12 | `WINGA26` | `0x09`–`0x0a` |
| `FDETXT63` | 62 | 12 | `WIN26` | — |
| `FDETXT64` | 63 | 34 | `WIN29` | `0x09`–`0x16` |
| `FDETXT65` | 64 | 20 | `GOODEND` | `0x09`–`0x13` |

沒有任何腳本切過去、整個區塊永遠不會載入的：`FDETXT31`（地圖 30）、`FDETXT32`（地圖 31）、`FDETXT34`（地圖 33）、`FDETXT50`（地圖 49）。有腳本切過去、但沒有一條被顯示的：`FDETXT42`、`FDETXT43`、`FDETXT63`。這些區塊下文不再列出，見 [`cut_content/`](../../cut_content/_index.md)。

## `FDETXT33`：地圖 32

切到地圖 32 的腳本：`ICON00`（第 1 章開場，`fdps_chapter_01_init`）。

### `0x09`（`ICON00` `0x013`）

```text
{speaker char=126}好吧，我該走了！
答應為村長買的藥，
我親自送去才放心。{page}
妳和蘭迪斯在家裡好好
待著，別到處亂走。
知道嗎？{page}
尤其是妳，瑪茜！
別再為孩子舉行什麼奇
怪的儀式了，{page}
若讓村人看見了，
不知惹出什麼事情來！{page}
妳要小心，
不要曝露自己的身份！
{speaker char=139}好了～！
快去吧！
巴特！你放心，{page}
你的兒子，
是萬神庇護的嬌兒，
沒人敢對你的寶貝兒{page}
子作什麼的！
{speaker char=126}好，親愛的，我走了！
{speaker char=139}巴特，你要快去快回喔
！
```

### `0x0a`（`ICON00` `0x048`）

```text
{speaker char=125}﹒﹒願以我神力護持，
賜我兒天上神鷹之眼，
地上雄獅之力量！
```

### `0x0b`（`ICON00` `0x050`）

```text
{speaker char=125}冥界眾神均迴避身影，
野獸與毒蛇聞聲趨避。
眾神垂憫憐聽，
```

### `0x0c`（`ICON00` `0x05b`）

```text
{speaker char=125}女武神瑪茜以子為誓，
謹請光明神賜以奇蹟，
願我兒生生世世永為{page}
汝之配劍護持﹒﹒
```

### `0x0d`（`ICON00` `0x0af`）

```text
{speaker char=129}不好了！不好了！
{speaker char=120}安靜！這裡是神殿！
別在這裏大呼小叫！
```

### `0x0e`（`ICON00` `0x0cc`）

```text
{speaker char=129}事情不好了！不好了！
{speaker char=120}什麼事情？
這麼慌慌張張的？
好像看到了妖怪～
{speaker char=129}是啊！是啊！
我看見巴特的太太把小
孩丟到火裡去，{page}
也不知道嘴裡唸了什麼
咒語，
小孩在火裡沒有燒傷，{page}
還在邊爬邊笑呢！
{speaker char=120}什麼？有這種事情？！
難怪巴特當年出現在我
們村莊，{page}
懇請我們收留他們時，
都不肯說明他們過去的
身份。{page}
哼～！
原來是異教的邪徒。
你先別急！{page}
且看我怎麼處置他們。
```

### `0x0f`（`ICON00` `0x0d4`）

```text
{speaker char=120}慢！在行動之前，
為了慎重起見，
我們還是要請示我們{page}
的神祇一下﹒﹒﹒
```

### `0x10`（`ICON00` `0x0e8`）

```text
{speaker char=120}我偉大崇高的神啊～！
世間萬事萬物的真相，
逃不過您銳利眼睛。{page}
請告訴我吧～！
定居在我們村子內，
巴特的妻子，{page}
究竟是不是邪惡的惡魔
化身？
```

### `0x11`（`ICON00` `0x15f`）

```text
{speaker char=120}去！
去將村民集合起來！
我們立刻要將村內{page}
的妖孽處置掉！
```

### `0x12`（`ICON00` `0x1d4`）

```text
{speaker char=120}把她抓起來！
燒了她！
```

## `FDETXT35`：地圖 34

切到地圖 34 的腳本：`ICON00`（第 1 章開場，`fdps_chapter_01_init`）。

### `0x09`（`ICON00` `0x233`）

```text
{speaker char=120}放火！
```

### `0x0a`（`ICON00` `0x2db`）

```text
{speaker char=118}這是怎麼回事？
你們快看～！
```

### `0x0b`（`ICON00` `0x310`）

```text
{speaker char=123}是誰在褻瀆神明？
竟然敢將我等之姐妹捆
綁在柴柱上燒化！{page}
幸而我等即時趕到！
我們的姐妹呢？
快將她交出來！{page}
否則休怪我不客氣了！
{speaker char=120}﹒﹒﹒啊？！
﹒﹒啊？！﹒﹒﹒
女神，請饒命～！{page}
我們不知道瑪茜的身份
所以誤會她是惡魔﹒﹒
可是她已經逃脫了，{page}
我們沒有傷害到她﹒﹒
真的﹒﹒
是真的﹒﹒﹒
{speaker char=123}胡說！
我等沒有感應到姐妹在
這附近，{page}
你們將她藏起來了！
是不是？！
{speaker char=120}啊？！沒有！沒有！
剛才這裡明明出現一個
漆黑的洞，{page}
將瑪茜吸進去後，
她便消失了！
{speaker char=121}這是妹妹的兒子嗎？
{speaker char=122}快讓我看看！～
```

### `0x0c`（`ICON00` `0x31e`）

```text
{speaker char=146}哇～！好可愛的孩子！
大姐，妳看，
是瑪茜的孩子耶～！
{speaker char=123}哼！小妹胡塗，
竟跟人類生出了孩子。
{speaker char=121}不管怎樣說，
他的體內仍有一半神的
血統，{page}
理應得到神的祝福，
這是他的權利。
{speaker char=123}不管妳們了！
小妹無緣無故消失，
這事情要趕快調查。{page}
不要在這裡浪費時間，
把孩子丟下，
我們快走吧！
{speaker char=146}唔～！好乖的小孩～！
我們先去找你媽媽了，
再見啦～！
```

### `0x0d`（`ICON00` `0x32b`）

```text
{speaker char=123}你們這些下界無知的人
無法分辨真神與假神，
我們並不加以怪罪。{page}
但是你們竟聽從讒言，
欲加害我等神族，
真是罪大惡極！{page}
今天便要你們見識神罰
的威力！
```

### `0x0e`（`ICON00` `0x3a3`）

```text
{speaker char=126}？
```

### `0x0f`（`ICON00` `0x3aa`）

```text
{speaker char=126}？？？
```

### `0x10`（`ICON00` `0x3b1`）

```text
{speaker char=126}？？？？？？？？
```

### `0x11`（`ICON00` `0x3b8`、`ICON00` `0x3c3`、`ICON00` `0x3dc`、`ICON00` `0x3e8`）

```text
{speaker char=126}瑪茜？！
```

### `0x12`（`ICON00` `0x3ff`）

```text
{speaker char=126}！
```

### `0x13`（`ICON00` `0x413`）

```text
{speaker char=126}蘭迪斯！
```

### `0x14`（`ICON00` `0x425`）

```text
{speaker char=126}﹒﹒﹒﹒﹒﹒？
```

## `FDETXT36`：地圖 35

切到地圖 35 的腳本：`ICON00`（第 1 章開場，`fdps_chapter_01_init`）。

### `0x04`（`ICON00` `0x493`）

```text
{speaker char=40}若干年後﹒﹒﹒
```

### `0x05`（`ICON00` `0x4b6`）

```text
{speaker unit=4}爸爸我回來了。
{speaker unit=3}回來了嗎？
唉﹒﹒﹒{page}
要不是爸爸得了重病，
也不會讓你年紀輕輕的
就出去工作﹒﹒﹒﹒﹒{page}
咳﹒﹒咳﹒﹒咳﹒﹒
{speaker unit=4}爸爸請您不要這樣說！
這是孩兒應該做的！{page}
外頭風大，您就進去裡
面休息吧﹒﹒﹒﹒
```

### `0x06`（`ICON00` `0x511`）

```text
{speaker char=0}爸爸您怎麼了？
快把藥喝了吧﹒﹒﹒
{speaker char=138}咳﹒﹒咳﹒﹒咳﹒﹒
不﹒﹒咳﹒﹒不用了！{page}
爸爸的﹒﹒狀況﹒﹒
自己﹒﹒最清楚了﹒﹒{page}
有件事﹒﹒一定﹒﹒
要告訴你﹒﹒﹒﹒
{speaker char=0}您別再說了！
先把藥喝了吧！
{speaker char=138}你讓我﹒﹒說完﹒﹒
我﹒﹒一直在騙你﹒﹒{page}
你的母親﹒﹒﹒﹒
瑪茜﹒﹒其實沒有死。
{speaker char=0}母親她﹒﹒﹒﹒
沒有死？？？
{speaker char=138}我從前﹒﹒對你﹒﹒
說過﹒﹒你媽﹒﹒
死於﹒﹒天災﹒﹒{page}
其實﹒﹒在我﹒﹒
找到你﹒﹒的那天﹒﹒{page}
並沒有﹒﹒發現﹒﹒
你母親﹒﹒的屍體﹒﹒{page}
你母親﹒﹒瑪茜她﹒﹒
或許是﹒﹒被誰﹒﹒
給帶走了﹒﹒﹒﹒{page}
雖然我﹒﹒一直﹒﹒
在尋找﹒﹒但卻﹒﹒{page}
一點蛛﹒﹒絲馬跡﹒﹒
也沒有﹒﹒﹒﹒
{speaker char=0}爸爸您就別再說了﹒﹒
{speaker char=138}蘭迪斯﹒﹒﹒﹒﹒﹒﹒
爸爸﹒﹒﹒對不﹒﹒﹒
起﹒﹒﹒﹒﹒﹒﹒﹒
{speaker char=0}爸爸﹒﹒﹒﹒﹒﹒
爸爸！！！！！！！
```

### `0x09`（`ICON00` `0x4d7`）

```text
{speaker char=0}爸爸，您的藥煎好了。
```

### `0x0a`（`ICON00` `0x4df`）

```text
{speaker char=138}咳﹒﹒咳﹒﹒咳﹒﹒
﹒﹒﹒﹒﹒﹒﹒﹒
{speaker char=0}爸爸！
```

### `0x0b`（`ICON00` `0x518`）

```text
{speaker char=0}爸爸﹒﹒﹒﹒
```

## `FDETXT37`：地圖 36

切到地圖 36 的腳本：`WIN00`（第 1 章勝利，`fdps_chapter_01_end`）。

### `0x09`（`WIN00` `0x020`）

```text
{speaker char=137}亞雷斯大臣，
宮裡到處找不到國王！
國王可能離宮兩天了。
{speaker char=14}唉～！知道了！
下去吧！
{speaker char=137}是！亞雷斯大臣！
```

### `0x0a`（`WIN00` `0x035`）

```text
{speaker char=91}亞雷斯大臣！
國王已經離開國境，
要不要派人去追？
{speaker char=14}唉～！
也只有如此了！
什麼時候不選，{page}
偏偏選在這個多事之秋
﹒﹒﹒﹒
快去吧！
{speaker char=91}是！
```

### `0x0b`（`WIN00` `0x056`）

```text
{speaker char=0}你真的實在太逞強了！
雖然幫你解了毒，
但是你還要多休息！
{speaker char=12}看見沒有？
這就是武士的精神，
只要想幹，{page}
沒有什麼辦不﹒不到。
哎喲～！痛死了！
{speaker char=0}你真是個奇怪的大叔！
好好的國王不做，
偏偏要出來流浪。{page}
結果不但被人暗算，
還差點丟了性命。
{speaker char=12}哈哈哈～！
你說的也對！
現在王宮裡的人，{page}
一定忙得手忙腳亂！
哇哈哈哈～﹒﹒﹒
{speaker char=0}你居然笑的這麼開心？
你果真是一個怪人！
{speaker char=12}唉～！你知道嗎？
人有了名譽與地位後，
什麼事情都改變了。{page}
什麼規矩﹑法度的，
弄得生活亂七八糟。
我雖是羅特帝亞國王，{page}
但是在我心裡，
真希望再退回到年青，
一天到晚餐風露宿，{page}
到處去旅遊冒險，
自由地去愛，去恨！{page}
那種在王宮裡，
錦衣玉食的生活，
真像是地獄一樣！{page}
等你再長大一點﹒﹒﹒
你就明白我說的話了。
{speaker char=0}聽了你的話，
你當了國王之後，
好像不很開心似的？
{speaker char=12}哈哈哈！或許是吧！
好，暫且不談這個了！
蘭迪斯！{page}
你父親已經去世了，
依我看，
你守著這座空屋，{page}
也不是辦法。
你的練武資質極佳，
面對敵人時，{page}
既冷靜又不害怕，
是塊當武士的料子，
如果一生活在山裡，{page}
只作個獵人樵夫，
實在浪費了你的天賦。
不如和我一起走吧！{page}
我把一生經驗傳授你，
讓你成為鐵錚錚的武士
如何？
{speaker char=0}﹒﹒﹒﹒
{speaker char=12}你父親曾告訴你，
你母親在你小時候，
因一場意外，{page}
而與你們父子失散了。
如果你母親尚在人間，
難道你不想見見，{page}
你世上唯一的親人嗎？
何不趁機會出去尋找？
也許真能找到線索，{page}
與母親團聚也說不定。
{speaker char=0}啊﹒﹒﹒﹒﹒
是的，
父親去世前，{page}
仍唸唸不忘著母親。
我不應該忘記，
我要找回自己母親！{page}
我決定了，
我跟你離開這裡！
{speaker char=12}很好！
就是這樣！
才像一個武士！{page}
蘭迪斯，
從今天開始，
你就是一個武士了！{page}
哈哈哈！有酒嗎？
我們來慶祝一下！
{speaker char=0}啊？！不行！
你的傷還沒好，
又要藉機喝酒了！
```

## `FDETXT38`：地圖 37

切到地圖 37 的腳本：`ICON09`（第 10 章開場，`fdps_chapter_10_init`）、`WIN09`（第 10 章勝利，`fdps_chapter_10_end`）。

### `0x09`（`ICON09` `0x0a6`）

```text
{speaker char=98}慢著！
你不交錢，
就想進城嗎？
{speaker char=0}什麼？
我只想進城看看而已。
這樣也要收錢？
{speaker char=98}呵呵呵～！
你們是外地人，
不跟你們計較了！{page}
嗯﹒﹒﹒
這架機器人很特別，
留下機器人，{page}
就當作你繳了過路稅。
好，
你們可以進城了！
```

### `0x0a`（`ICON09` `0x0a8`）

```text
{speaker char=4}喂！喂！
你們太過份了！
我旅行這麼多地方，{page}
從來沒見過像你們，
這麼霸道的！
{speaker char=98}什麼！
你敢頂嘴？{page}
你污辱了神聖的主教。
給我全部抓起來！
送他們上宗教法庭！
```

### `0x0b`（`ICON09` `0x0c1`）

```text
{speaker char=3}真是豈有此理﹒﹒﹒
{speaker char=1}不要碰我！﹒﹒﹒
```

### `0x0c`（`WIN09` `0x12c`）

```text
{speaker char=4}好呀！尤利安，
你騙了我們好久，
你為什麼不告訴我，{page}
你就是教皇啊！
{speaker char=6}唉～！
今天要不是情況危急，
我是不想揭露身份的。{page}
因為我怕身份曝露後，
你們就不願再和我在一
起了。
{speaker char=0}怎麼會呢？
我們不都是好朋友嗎？
怎麼會因為你是教皇，{page}
就離開你呢？
你們說是不是？
{speaker char=0}放心吧！
我們仍當你是尤利安！
我可不想少一個，{page}
能幫我的小伙子啊！
呵呵呵～！
{speaker char=6}啊！
真的嗎？{page}
那太好了！
從今以後，
我還是那個呆頭呆腦，{page}
小僧侶尤利安就是了！
{speaker char=8}啊！
這可不行！
除非你發誓；{page}
你不會拿出教皇身份，
強迫我跟你一起祈禱。
{speaker char=6}哈哈哈～哈！
```

## `FDETXT39`：地圖 38

切到地圖 38 的腳本：`ICON09`（第 10 章開場，`fdps_chapter_10_init`）。

### `0x09`（`ICON09` `0x210`）

```text
{speaker char=124}咳～！肅靜！
現在要審判的案件是，
蘭迪斯等一行人，{page}
聚眾滋擾城門士兵，
干預警務﹑污辱主教—
咳～！污辱本人。{page}
被告蘭迪斯，
對於上述指控，
你有什麼話要說？
{speaker char=0}慢一點！
我聚眾滋擾城門士兵？
怎麼可能？
{speaker char=124}哦？
那你反對這項指控了？{page}
好，我問你，
你旁邊這些人，
是不是你的朋友？
{speaker char=0}是的！
{speaker char=124}哦？
這些人當時在不在場？
{speaker char=0}在場！
{speaker char=124}你進城門時，
有沒有乖乖的，
將過路稅交給士兵？
{speaker char=0}沒有！
{speaker char=124}你有沒有反抗？
{speaker char=0}可是，
這是因為﹒﹒﹒﹒
{speaker char=124}沒有什麼可是的。
好！被告蘭迪斯，
承認聚眾滋擾一事。{page}
紀錄﹒﹒﹒﹒
```

### `0x0a`（`ICON09` `0x212`）

```text
{speaker char=124}咳～！被告蘭迪斯，
你對於你干預警務，
和污辱本人的指控。{page}
你還有什麼理由，
需要為你申訴？
{speaker char=0}我沒有干預警務，
更沒有污辱主教你！
{speaker char=124}哦？你的意思是，{page}
要反駁這兩項指控了？
好，那我再問你，
當士兵特別通融，{page}
讓你以機器人抵稅時，
你是不是拒絕交出？
{speaker char=0}那是當然的！
蓋亞又不是你們的﹒﹒
{speaker char=124}好！被告蘭迪斯，
已承認干預警務一事。
紀錄﹒﹒﹒﹒
```

### `0x0b`（`ICON09` `0x214`）

```text
{speaker char=1}這是什麼審判？
﹒﹒﹒根本是私刑嘛！
{speaker char=124}肅靜～！
這裡是神聖的法庭，
不許在此大聲說話。{page}
有關你褻瀆法庭之罪，
等一下再一併加算。{page}
被告蘭迪斯，
你還有什麼話要說？
{speaker char=0}這﹒﹒﹒這﹒﹒﹒
在城門口我沒污辱你，
是士兵說的，我沒說
{speaker char=124}哦？是嗎？
你沒有聽從士兵的話，
交出你的機器人。{page}
是不是違背本城條律？
而條律是由我訂定的，
你既然不遵守條律，{page}
看輕條律，
還和執行條律的士兵，
大打出手，{page}
你說，
那是不是已經污辱了，
訂定條律的我呢？
{speaker char=0}這﹒﹒這個﹒﹒
{speaker char=124}好！被告蘭迪斯，
已承認污辱本人一事。
紀錄﹒﹒﹒﹒{page}
現在，
宣判被告蘭迪斯罪名；
聚眾滋擾士兵﹑{page}
干預警務﹑污辱主教，
連同褻瀆法庭，
嗯﹒﹒﹒﹒{page}
被告等人判處死刑！
{speaker char=0}什麼？
死﹒﹒死刑！
你們太過份了！
{speaker char=4}簡直是胡來嘛！
哪有這種事？
{speaker char=6}﹒﹒﹒可惡﹒﹒﹒
```

### `0x0c`（`ICON09` `0x216`）

```text
{speaker char=124}慢著！
安靜～！{page}
本庭姑念你們，
不熟悉本城條律，
又是初犯。{page}
這件事情，
可以給你們一點通融，
只要你留下機器人，{page}
再交出一百萬贖罪金，
本庭就考慮釋放你們
{speaker char=6}慢著！
```

### `0x0d`（`ICON09` `0x21e`）

```text
{speaker char=6}你已經沒有資格，
在這裡審判了，
主教！{page}
我命令你停止審判，
並交出職務，
等待中央樞機卿處份
{speaker char=0}這是怎麼回事？
{speaker char=4}喂！尤利安，
你是不是急瘋了？
{speaker char=3}嘿！
看不出來耶！{page}
平時愣頭呆腦的，
想不到他一站出去，
講起話來蠻威嚴的！
{speaker char=124}慢著！你是誰？
竟敢如此大膽冒犯我！
{speaker char=6}你多行不義，
今天罪證全在我手中，
你還有何面目，{page}
在這裡擔任神職？
你問我是誰？
抬起你的臉，{page}
張大眼睛看清楚。
我就是當今的教皇，
聖杜克蘭十七世，{page}
你們每年回到中央，
參加聖月彌撒，{page}
難道連當今教皇，
都不認得了嗎？
還不過來跪下參拜！
{speaker char=4}這﹒﹒﹒﹒
這傻小子﹒﹒
教皇﹒﹒
{speaker char=2}早聽說當今的教皇，
年紀非常輕，
沒想到，{page}
他竟然會化裝成僧侶，
混在我們當中﹒﹒﹒
{speaker char=0}教皇﹒﹒﹒﹒
尤利安是教皇？
{speaker char=124}﹒﹒﹒可惡！﹒﹒﹒
我辛辛苦苦聚集財富，
怎麼可以輕易放棄？{page}
我只要把你們殺了，
不讓消息洩露，
今天這裡發生的事，{page}
永遠不會有人知道了！
來人呀！
這群人意圖謀刺主教，{page}
趕快把他們殺了！
{speaker char=0}尤利安，快逃！
他們想殺人滅口了！
```

## `FDETXT40`：地圖 39

切到地圖 39 的腳本：`WIN03`（第 4 章勝利，`fdps_chapter_04_end`）。

### `0x09`（`WIN03` `0x03c`）

```text
{speaker char=91}找到國王了！
國王回來了！
{speaker char=14}是真的嗎？
```

### `0x0a`（`WIN03` `0x062`）

```text
{speaker char=117}啊？！亞雷斯，
真不好意思！
只是去透透氣而已，{page}
沒想到，
引起這麼大的騷動。
哈哈哈～
{speaker char=14}﹒﹒﹒﹒﹒﹒
```

## `FDETXT41`：地圖 40

切到地圖 40 的腳本：`ICON11`（第 12 章開場，`fdps_chapter_12_init`）。

本區塊另有 6 條有內容、但沒有腳本引用的條目，永遠不會顯示，見 [`cut_content/`](../../cut_content/_index.md)。

### `0x0a`（`ICON11` `0x0fd`）

```text
{speaker char=122}火神，
這是武神瑪茜，
與人類生的孩子！{page}
我已經將他帶來了。
請照原先的約定，
將武器賜給他！
```

### `0x0b`（`ICON11` `0x150`）

```text
{speaker char=127}孩子，
要使用神的兵器，{page}
就要證明你，
有擁有它的資格；{page}
這是我的規定，
就算是神，
要取我的兵器，{page}
也要遵守這規定。
```

### `0x0c`（`ICON11` `0x16a`）

```text
{speaker char=122}火神已同意了，
現在你們跟我來吧。
```

### `0x0d`（`ICON11` `0x252`）

```text
{speaker char=122}看！
```

### `0x0e`（`ICON11` `0x2ab`）

```text
{speaker char=122}蘭迪斯，
這三間房間裡，
各藏著一件神兵利器
```

### `0x0f`（`ICON11` `0x2c2`）

```text
{speaker char=122}你只能選擇其中一件，
作為你的武器。{page}
只要你打敗守護獸，
你就可擁有那件兵器。{page}
而你現在必須要決定
要進到哪一個房間﹒﹒
```

### `0x10`（`ICON11` `0x2c4`）

```text
左方的房間裡是修佩魯
你要進去左邊的房間嗎
？
```

### `0x11`（`ICON11` `0x2c4`）

```text
{speaker char=122}決定是修配魯了嗎？
```

### `0x12`（`ICON11` `0x2c4`）

```text
{speaker char=122}那麼去吧，
孩子，祝福你！
```

### `0x13`（`ICON11` `0x2c4`）

```text
中間的房間裡是雷德
你要進去中間的房間嗎
？
```

### `0x14`（`ICON11` `0x2c4`）

```text
{speaker char=122}決定是雷德了嗎？
```

### `0x15`（`ICON11` `0x2c4`）

```text
{speaker char=122}那麼就是右方的
亞德尼恩了﹒﹒﹒
```

## `FDETXT44`：地圖 43

切到地圖 43 的腳本：`WIN00`（第 1 章勝利，`fdps_chapter_01_end`）。

本區塊另有 4 條有內容、但沒有腳本引用的條目，永遠不會顯示，見 [`cut_content/`](../../cut_content/_index.md)。

### `0x09`（`WIN00` `0x0b3`）

```text
{speaker char=122}孩子，
好久不見了﹒﹒﹒
{speaker char=0}妳﹒﹒妳是誰？
我以前見過妳嗎？
{speaker char=122}你不記得了嗎？唉～！
我是女武神絲卡蒂亞，
你母親的姐妹，{page}
這麼多年了，
你終於要出發，
去尋找妳母親了嗎？
{speaker char=0}是的，
妳怎麼會知道的？
{speaker char=122}唉！傻孩子，
你忘了我是女武神呀！
{speaker char=0}啊！
那麼妳知道，
我母親的下落了？！
{speaker char=122}對不起﹒﹒﹒﹒
我不知道你母親下落。
{speaker char=0}﹒﹒﹒﹒﹒﹒
{speaker char=122}不要傷心！
雖然我不知道，
你母親真正的下落，{page}
但是我可以看見你，
和你母親的命運之線，
它們是連在一起。{page}
只要你不放棄希望，
命運之線會指引你，
讓你見到母親。
{speaker char=0}真的？！
{speaker char=122}孩子，你記著，
你母親是我們的姐妹。
你的身上，{page}
同樣流著神族血液，
是神族的一份子。
你要珍惜著，{page}
身為神族的榮耀，
神將永遠與你同在。
蘭迪斯，眼睛閉上。{page}
這是你的守護之神—
火神送給你的禮物。
來，傾聽那聲音﹒﹒{page}
讓生命之風，
喚醒在軀殼中的靈魂。
```

### `0x0a`（`WIN00` `0x0c5`）

```text
{speaker char=122}好了﹒﹒﹒
你有施展魔法的能力。
我的任務達成了，{page}
孩子，
再見了﹒﹒﹒﹒﹒
```

### `0x0b`（`WIN00` `0x0cf`）

```text
{speaker char=0}真是太不可思議了﹒﹒
我真會施展魔法了嗎？
來試試看！
```

## `FDETXT45`：地圖 44

切到地圖 44 的腳本：`WIN18`（第 19 章勝利，`fdps_chapter_19_end`）。

本區塊另有 4 條有內容、但沒有腳本引用的條目，永遠不會顯示，見 [`cut_content/`](../../cut_content/_index.md)。

### `0x09`（`WIN18` `0x0c8`）

```text
{speaker char=63}可惡！
這個小丫頭，
真是天不怕地不怕，{page}
居然跑去和敵人廝混。
真不知道她在想什麼？
凱因巴，帶她回來！
{speaker char=67}教主，
小姐恐怕不會理會您。
不如這樣吧！{page}
讓屬下和小姐談一談，
若小姐肯回來，
自然上上大吉。{page}
若小姐不肯回來，
讓她留在敵人中，
為我們通風報信。{page}
豈不更容易掌握敵蹤？
這是屬下的一點意見，
不知教主以為如何？
{speaker char=63}嗯～！很好，
就這麼辦！
不過，{page}
最好是能帶她回來。
要啟封魔神的封印，
還是需要她呀！
{speaker char=67}是！
屬下知道了。
```

## `FDETXT46`：地圖 45

切到地圖 45 的腳本：`WIN17`（第 18 章勝利，`fdps_chapter_18_end`）、`WIN17-1`（第 18 章勝利，`fdps_chapter_18_end`）。

本區塊另有 4 條有內容、但沒有腳本引用的條目，永遠不會顯示，見 [`cut_content/`](../../cut_content/_index.md)。

### `0x09`（`WIN17` `0x05b`）

```text
{speaker char=12}很遺憾，
還是沒能追到他！{page}
不過沒有想到他還留有
伏兵﹒﹒﹒{page}
﹒﹒﹒話說回來，
真是英雄出少年！
蘭迪斯，{page}
你終於成為一個，
頂天立地的武士了！
我真是為你高興！
{speaker char=0}謝謝﹒﹒﹒﹒
{speaker char=12}呵呵呵～呵！
謝什麼？
難道你忘了？{page}
不久之前，
我們還搶著吃一鍋飯！
喂！老伙伴，{page}
是不是這樣呀？
尤利安？
亞克？
{speaker char=6}哈哈哈～哈！
{speaker char=4}哈哈哈～哈！
{speaker char=12}現在的羅特帝亞，
還有一堆問題，
等著我去善後，{page}
我暫時無法抽身，
跟你們一起離開。{page}
不過沒有關係，
羅特帝亞的大門，
永遠為你們而開。{page}
要是你們遇上困難，
送來一句話，
我立刻趕去，{page}
這是武士之間的承諾，
知道嗎？
{speaker char=0}謝謝你，索爾！
{speaker char=12}呵呵呵～呵！
真的是好久不見了！{page}
看看你，
快長的跟我一樣高了。
{speaker char=0}﹒﹒﹒﹒﹒
```

### `0x0a`（`WIN17-1` `0x05b`）

```text
{speaker char=12}很遺憾，
還是沒能追到他！{page}
不過沒有想到他還留有
伏兵﹒﹒﹒{page}
﹒﹒﹒話說回來，
真是英雄出少年！
蘭迪斯，{page}
你終於成為一個，
頂天立地的武士了！
我真是為你高興！
{speaker char=0}謝謝﹒﹒﹒﹒
{speaker char=12}呵呵呵～呵！
謝什麼？
難道你忘了？{page}
不久之前，
我們還搶著吃一鍋飯！
喂！老伙伴，{page}
是不是這樣呀？
尤利安？
亞克？
{speaker char=6}哈哈哈～哈！
{speaker char=4}哈哈哈～哈！
{speaker char=12}你身上的這個徽章真是
漂亮。
{speaker char=0}這個神的聖印嗎？
{speaker char=12}對！就是這個﹒﹒﹒﹒
不過我好像在哪裡見過
的樣子﹒﹒﹒﹒{page}
啊！！！
這個不是勇者徽章嗎！
{speaker char=0}勇者徽章？
{speaker char=12}對對對！就是勇者徽章
只是名稱不一樣罷了吧。
{speaker char=0}勇者徽章﹒﹒﹒﹒
{speaker char=12}現在的羅特帝亞，
還有一堆問題，
等著我去善後，{page}
我暫時無法抽身，
跟你們一起離開。{page}
不過沒有關係，
羅特帝亞的大門，
永遠為你們而開。{page}
要是你們遇上困難，
送來一句話，
我立刻趕去，{page}
這是武士之間的承諾，
知道嗎？
{speaker char=0}謝謝你，索爾！
{speaker char=12}呵呵呵～呵！
真的是好久不見了！{page}
看看你，
快長的跟我一樣高了。
{speaker char=0}﹒﹒﹒﹒﹒
```

## `FDETXT47`：地圖 46

切到地圖 46 的腳本：`WIN05`（第 6 章勝利，`fdps_chapter_06_end`）。

本區塊另有 4 條有內容、但沒有腳本引用的條目，永遠不會顯示，見 [`cut_content/`](../../cut_content/_index.md)。

### `0x09`（`WIN05` `0x029`）

```text
{speaker char=12}對不起，蘭迪斯﹒﹒﹒
讓你們捲進這場麻煩，
這事自始至終，{page}
都是因我的任性引起，
我必須要為它負責！
謝謝你們，{page}
給我這段快樂的日子。
蘭迪斯，希望有一天，
還能再看見你，{page}
再見了！﹒﹒﹒﹒
```

### `0x0a`（`WIN05` `0x07e`）

```text
{speaker char=0}他﹒﹒他還是走了﹒﹒
{speaker char=1}蘭迪斯，不要傷心﹒﹒
身為羅特帝亞的國王，
他有他的尊嚴苦衷﹒﹒
{speaker char=4}他不願拖累我們。
不論敵人有多可怕，
他也要獨自面對敵人。{page}
唉～！
不愧是傳說中的英雄！
{speaker char=6}每個人的一生中，
都有順境與逆境。{page}
唯一不同之處在於，
勇敢的人選擇去面對，
膽小的人卻選擇逃避。{page}
現在的他，
選擇了自己要走的路。
再見了！{page}
羅特帝亞的王者！
我們不會忘記你的。
```

## `FDETXT48`：地圖 47

切到地圖 47 的腳本：`ICON06`（第 7 章開場，`fdps_chapter_07_init`）、`WIN24`（第 25 章勝利，`fdps_chapter_25_end`）。

本區塊另有 4 條有內容、但沒有腳本引用的條目，永遠不會顯示，見 [`cut_content/`](../../cut_content/_index.md)。

### `0x09`（`ICON06` `0x044`）

```text
{speaker char=1}哇！
這裡好熱鬧！
```

### `0x0a`（`ICON06` `0x04b`）

```text
{speaker char=0}咦？
那裡好像貼著什麼？
我們去看看！
```

### `0x0b`（`ICON06` `0x072`）

```text
{speaker char=0}紙上面寫著﹕
有意參加武鬥會者，
請將此單撕下。裘娜
{speaker char=4}不知道他們功夫怎樣？
真想和他們較量一下！
{speaker char=6}我們是出來旅行的，
參觀一下比賽就好了。
還是別惹麻煩了！
{speaker char=1}說得也是，
別到處想惹事生非，
我們快走吧！
```

### `0x0c`（`ICON06` `0x0cc`）

```text
{speaker char=118}哎呀呀呀～，
好久沒人敢撕榜單了。
這下可有好戲看了！
{speaker char=119}看這幾位年紀輕輕的，
希望不要白白送了性命
{speaker char=130}你這是說什麼話？
人家要是沒有本事，
敢撕這榜單嗎？{page}
這才叫來者不善，
藝高人膽大，懂嗎？
{speaker char=6}﹒﹒慘了，蘭迪斯﹒﹒
{speaker char=0}唔，好像，
現在說什麼也沒用了。
{speaker char=4}打就打嘛！
幹嘛愁眉苦臉的﹒﹒
{speaker char=6}神呀！﹒﹒
{speaker char=1}嗚﹒﹒討厭，
受傷很痛的耶～
你們這些臭男生！
```

## `FDETXT49`：地圖 48

切到地圖 48 的腳本：`ICON07`（第 8 章開場，`fdps_chapter_08_init`）。

本區塊另有 4 條有內容、但沒有腳本引用的條目，永遠不會顯示，見 [`cut_content/`](../../cut_content/_index.md)。

### `0x09`（`ICON07` `0x014`）

```text
{speaker char=0}這是什麼聲音？
```

### `0x0a`（`ICON07` `0x033`）

```text
{speaker char=3}好像在前面森林裡。
{speaker char=4}去看看是什麼？
走啊！
還等什麼？
```

### `0x0b`（`ICON07` `0x085`）

```text
{speaker char=0}你是誰？
為何要引我們到這裡？
{speaker char=2}我是魔導士費塔加；
請問你們來這裡，
是為了尋找鎮民嗎？
{speaker char=6}是呀！
難道﹒﹒你也是嗎？
{speaker char=2}﹒﹒嗯！
我剛才發現村人，
被囚禁在石窟中。{page}
需要有人在前面，
引開守衛的注意，
另外有人繞到後面，{page}
將囚禁的鎮民放出來。
太好了！
你們來的正是時候！
{speaker char=4}你要我們怎麼幫你？
{speaker char=2}我需要你們在洞窟前，
引誘守衛離開柵欄。{page}
然後，
我會在後方炸開個洞。
從那裡進來，{page}
然後將柵欄打開，
放出鎮民，
送他們從洞口逃脫。{page}
只要鎮民逃離洞窟，
我們就達成任務了！
{speaker char=1}是誰這麼狠心？
居然把鎮民捉起來，
囚禁在這裡？
{speaker char=2}﹒﹒﹒﹒
聽說是羅特帝亞國王，
為了蓋一座教塔，{page}
命令手下捉拿奴工﹒﹒
{speaker char=0}這是不可能的！
{speaker char=2}﹒﹒﹒為什麼？
{speaker char=0}我知道的索爾，
是不會幹這種事的！
{speaker char=2}哦？是嗎？﹒﹒﹒
{speaker char=6}蘭迪斯，
這事情還有很多疑點，
我們一步步慢慢查，{page}
好嗎？
我們的當務之急，
是拯救無辜的鎮民。{page}
費塔加先生，
我們分頭行事，
請你為我們帶路。
{speaker char=2}好，我來替你們帶路。
```

## `FDETXT51`：地圖 50

切到地圖 50 的腳本：`WIN22`（第 23 章勝利，`fdps_chapter_23_end`）。

本區塊另有 4 條有內容、但沒有腳本引用的條目，永遠不會顯示，見 [`cut_content/`](../../cut_content/_index.md)。

### `0x09`（`WIN22` `0x104`）

```text
{speaker char=1}我﹒﹒﹒我走了，
他﹒﹒﹒他﹒﹒﹒﹒
你們好好照顧他吧！
{speaker char=6}法蓮娜﹒﹒﹒﹒
{speaker char=5}嗚嗚～嗚！
大姐姐﹒﹒﹒﹒﹒
```

### `0x0a`（`WIN22` `0x1b5`）

```text
{speaker char=0}法蓮娜～！
```

### `0x0b`（`WIN22` `0x1eb`）

```text
{speaker char=0}法蓮娜～！
妳真的要離開我嗎？
```

### `0x0c`（`WIN22` `0x200`）

```text
{speaker char=1}﹒﹒﹒﹒對不起，
我不是故意要對你說謊
的﹒﹒﹒﹒
```

### `0x0d`（`WIN22` `0x238`）

```text
{speaker char=0}法蓮娜～！！
```

### `0x0e`（`WIN22` `0x2a2`）

```text
{speaker char=4}糟糕！他暈過去了！
他病體初癒，
受的刺激太大了﹒﹒﹒
{speaker char=2}快把他抬進去！
{speaker char=7}嗚嗚嗚～嗚﹒﹒﹒
大哥哥﹒﹒﹒
```

## `FDETXT52`：地圖 51

切到地圖 51 的腳本：`WIN20`（第 21 章勝利，`fdps_chapter_21_end`）、`WIN24`（第 25 章勝利，`fdps_chapter_25_end`）。

本區塊另有 6 條有內容、但沒有腳本引用的條目，永遠不會顯示，見 [`cut_content/`](../../cut_content/_index.md)。

### `0x0b`（`WIN20` `0x0ee`）

```text
{speaker char=2}這裡就是葛斯洛喀教，
用來祭祀的神殿？！
看！{page}
那是他們祭祀的主神，
平衡之神！
```

### `0x0c`（`WIN20` `0x33b`）

```text
{speaker char=0}這是什麼？
調查一下看看！
```

### `0x0d`（`WIN20` `0x3a7`）

```text
{speaker char=1}蘭迪斯！蘭迪斯！～
```

### `0x0e`（`WIN20` `0x41c`）

```text
{speaker char=3}？！﹒﹒﹒﹒
死﹒﹒死了！
不好了！尤利安！
{speaker char=6}？！﹒﹒﹒﹒
什麼？！
不可能的！{page}
蘭迪斯的生命之火，
已經熄滅了﹒﹒﹒
救不活了﹒﹒﹒﹒
{speaker char=7}大哥哥～！
醒一醒！
你不要死呀！
{speaker char=5}嗚嗚嗚～嗚﹒﹒﹒
怎麼會這樣呢？
{speaker char=4}可惡～！！
這些狗賊，
這麼卑鄙！
{speaker char=8}尤利安！！
真的沒有辦法了嗎？
{speaker char=6}﹒﹒我﹒﹒﹒
﹒﹒十分抱歉﹒﹒
```

### `0x0f`（`WIN20` `0x4cd`）

```text
{speaker char=3}可惡～！
惡賊，看刀！
{speaker char=4}殺了這個惡賊，
替蘭迪斯報仇！
{speaker char=67}咯咯咯～咯！
慢著！慢著！
我不是為打架而來。{page}
你們救不成蘭迪斯，
我倒有個方法可救他。{page}
你們殺了我，
就救不成他了！
{speaker char=4}亞克！
裘娜！
慢一點動手！{page}
你說，
你有什麼方法，
可以救活蘭迪斯？
{speaker char=67}咯咯咯～咯！
要我說出方法是可以，
但我有一個交換條件；{page}
你們必須答應我，
讓我帶走一個人。{page}
如果你們願意的話，
我就把方法告訴你們！
{speaker char=6}一個人？！
{speaker char=67}小姐！
教主命令屬下
迎接小姐回去，{page}
小姐卻執意不肯，
屬下只有出此下策，
希望小姐見諒。{page}
屬下可以說出，
救蘭迪斯的方法，
但要談一個條件；{page}
希望小姐救了蘭迪斯，
能夠跟屬下一同回去，
讓屬下交差了事。{page}
小姐！
不知妳同意不同意？
```

### `0x10`（`WIN20` `0x4cf`）

```text
{speaker char=6}法蓮娜﹒﹒妳﹒﹒
{speaker char=2}唉﹒﹒﹒﹒﹒﹒
{speaker char=4}﹒﹒﹒真不敢相信，
法蓮娜你﹒﹒﹒？！
{speaker char=7}大姐姐？！
```

### `0x11`（`WIN20` `0x4d1`）

```text
{speaker char=1}﹒﹒﹒﹒﹒﹒
你﹒﹒你說吧﹒﹒
我答應﹒﹒﹒{page}
我跟你回去﹒﹒﹒﹒
{speaker char=3}法蓮娜﹒﹒﹒﹒
{speaker char=67}謝謝小姐！
蘭迪斯的生命之火，
已經熄滅，{page}
魂魄已經渡過了死河。
現在要救蘭迪斯，
方法只有一個，{page}
就是到巫湯婆婆那裡，
取得失魂藥。{page}
人喝下失魂藥，
魂魄會暫時離開軀殼。{page}
你們喝下失魂藥，
就可以渡過死河，
奪回蘭迪斯的魂魄。{page}
不過要小心呀！
萬一你們失敗了，
你們的魂魄，{page}
就只有留在死神那裡。
咯咯咯～咯！
﹒﹒﹒我走了！
```

### `0x12`（`WIN20` `0x5dd`）

```text
{speaker char=1}走！
去找巫湯婆婆，
我們要拿到失魂藥！
```

### `0x13`（`WIN20` `0x295`）

```text
{speaker char=0}那是什麼！
去看看～！
{speaker char=2}小心！
說不定是敵人的詭計！
```

## `FDETXT53`：地圖 52

切到地圖 52 的腳本：`ICON11`（第 12 章開場，`fdps_chapter_12_init`）。

本區塊另有 9 條有內容、但沒有腳本引用的條目，永遠不會顯示，見 [`cut_content/`](../../cut_content/_index.md)。

### `0x09`（`ICON11` `0x0a1`）

```text
{speaker char=122}孩子，來，跟我走！
{speaker char=0}走？我們要去哪裡？
{speaker char=122}傻孩子，
幫你找一件武器呀！
把你的伙伴帶來吧！{page}
這件事有點困難，
沒他們幫忙不行的。
```

## `FDETXT54`：地圖 53

切到地圖 53 的腳本：`WIN11`（第 12 章勝利，`fdps_chapter_12_end`）。

本區塊另有 9 條有內容、但沒有腳本引用的條目，永遠不會顯示，見 [`cut_content/`](../../cut_content/_index.md)。

### `0x09`（`WIN11` `0x031`）

```text
{speaker char=122}孩子，好好地利用它。
﹒﹒﹒我走了﹒﹒﹒
{speaker char=0}慢﹒﹒慢著！
```

### `0x0a`（`WIN11` `0x044`）

```text
{speaker char=6}好像是一場夢﹒﹒﹒﹒
{speaker char=2}﹒﹒真是不可思議﹒﹒
{speaker char=7}哎呀！你們看～！
{speaker char=0}這不是夢！
```

## `FDETXT55`：地圖 54

切到地圖 54 的腳本：`ICON08`（第 9 章開場，`fdps_chapter_09_init`）。

本區塊另有 4 條有內容、但沒有腳本引用的條目，永遠不會顯示，見 [`cut_content/`](../../cut_content/_index.md)。

### `0x09`（`ICON08` `0x028`）

```text
{speaker char=1}快！快！
表演就要開始了！
{speaker char=4}人真能靠機器飛上天？
哈哈哈～！
我才不信呢！
```

### `0x0a`（`ICON08` `0x054`）

```text
{speaker char=8}咳～！
大家可要睜大眼睛！
看清楚啦！{page}
這將會是人類第一次，
成功地飛上天空，
我布蘭多，{page}
就是創造這紀錄的人，
呵呵呵～！
現在我要試飛了！
```

### `0x0b`（`ICON08` `0x06f`）

```text
{speaker char=4}咦？！哎呀？
奇怪？
這東西真的可以飛！
```

### `0x0c`（`ICON08` `0x093`）

```text
{speaker char=1}哎呀～！{page}
你們看那裡～！
他掉下去了！{page}
蘭迪斯，
我們快追上去！
{speaker char=0}好！我們走！
```

## `FDETXT56`：地圖 55

切到地圖 55 的腳本：`ICON08`（第 9 章開場，`fdps_chapter_09_init`）。

本區塊另有 5 條有內容、但沒有腳本引用的條目，永遠不會顯示，見 [`cut_content/`](../../cut_content/_index.md)。

### `0x09`（`ICON08` `0x0cf`、`ICON08` `0x0eb`、`ICON08` `0x102`）

```text
{speaker char=8}這裡是哪裡呀？
有人在嗎～？
```

### `0x0a`（`ICON08` `0x115`）

```text
{speaker char=8}咦？這個機器人是﹒﹒
呀！正好口袋裡，
還有個飛行器電池！{page}
我來試試看吧！
```

### `0x0b`（`ICON08` `0x12a`）

```text
{speaker char=9}嗶﹒﹒吱吱﹒﹒
{speaker char=8}咦？真的動了？
```

### `0x0c`（`ICON08` `0x159`）

```text
{speaker char=8}咦？
你不要一直跟著我嘛！
真是糟糕！{page}
要是能出去就好了！
{speaker char=9}嗶﹒﹒吱吱﹒﹒咯咯
```

## `FDETXT57`：地圖 56

切到地圖 56 的腳本：`ICON24`（第 25 章開場，`fdps_chapter_25_init`）。

本區塊另有 9 條有內容、但沒有腳本引用的條目，永遠不會顯示，見 [`cut_content/`](../../cut_content/_index.md)。

### `0x09`（`ICON24` `0x0a5`）

```text
{speaker char=67}教主，
屬下將小姐帶回來了。
{speaker char=63}嗯～！幹得很好。
法蓮娜，
頑皮也要有限度。{page}
妳私自離開城堡，
為了妳，
我犧牲了多少時間？{page}
妳看看，
為了迎接魔神的降臨，
父親花了多少心血，{page}
在這個重要的時刻，
妳還要讓父親分心，
擔心妳的事嗎？
{speaker char=1}我這次出去，
看見外面不少東西。
爸爸，{page}
我們真的有必要，
讓魔神降臨到人間嗎？{page}
難道世間的事情，
人類難道就辦不到？
都要靠神來解決嗎？
{speaker char=63}住口～！
妳這樣褻瀆神，
不怕祂降罪？{page}
妳懂什麼？
神是無所不能的，
最重要的是，{page}
祂還允諾給我們永生。
永生是什麼意思？{page}
妳不必為生﹑老﹑病﹑
死擔憂苦惱；
妳能夠享受妳的財富﹑{page}
權勢﹑幸運，
十﹑二十﹑一百年，
哈哈哈～哈，{page}
它永遠握在你手掌中，
這才是我真正要的，
妳懂嗎？
{speaker char=1}爸爸，
你太瘋狂了！﹒﹒﹒
{speaker char=67}教主，
有件事情要向你報告，
蘭迪斯這批人，{page}
似乎還不死心的樣子，
據教徒密報，
他們已經來了！
{speaker char=63}哦？！有這種事？
你們四位魔戰將軍，
身為本教護法，{page}
居然容許他如此猖狂？
將他們除去，
不要手下留情。{page}
我和法蓮娜，
要舉行降神的儀式，
沒有辦法分心照顧，{page}
凱因巴，
這件事交給你去辦！
{speaker char=1}爸爸！！
{speaker char=67}是！屬下知道了！
屬下立刻去辦！
```

## `FDETXT58`：地圖 57

切到地圖 57 的腳本：`WIN08`（第 9 章勝利，`fdps_chapter_09_end`）。

本區塊另有 9 條有內容、但沒有腳本引用的條目，永遠不會顯示，見 [`cut_content/`](../../cut_content/_index.md)。

### `0x09`（`WIN08` `0x018`）

```text
{speaker char=0}還好﹒﹒﹒
他們沒有追過來。
{speaker char=1}布蘭多爺爺，
你大鬧宮殿，
搶走他們的機器人，{page}
我看你在這裡，
恐怕要被列入，
懸賞人物名單了！
{speaker char=8}哎～呀呀呀！
這個話該怎麼講呢？
唉～！真是的﹒﹒
{speaker char=9}嗶﹒﹒吱吱﹒﹒咯咯
```

### `0x0a`（`WIN08` `0x031`）

```text
{speaker char=3}喂～！
我看見衛兵追過來了！
還不快跑！
```

### `0x0b`（`WIN08` `0x087`）

```text
{speaker char=76}呀！
你們還沒走！
捉起來！
```

## `FDETXT59`：地圖 58

切到地圖 58 的腳本：`ICON19`（第 20 章開場，`fdps_chapter_20_init`）。

本區塊另有 13 條有內容、但沒有腳本引用的條目，永遠不會顯示，見 [`cut_content/`](../../cut_content/_index.md)。

### `0x09`（`ICON19` `0x01f`）

```text
{speaker char=0}不！
我不會相信的！
你走吧！
{speaker char=2}不，你聽我說，
蘭迪斯，
我又何嘗願意相信？{page}
法蓮娜身份太可疑了；
還記得嗎？
在羅特帝亞王宮中，{page}
法蓮娜在大家的面前，
放走假冒索爾的首領。
索爾事後一字不提。{page}
以他嫉惡如仇的個性，
居然按捺得下性子，
你不覺得反常嗎？{page}
唉～！﹒﹒﹒﹒
蘭迪斯，
那是因為你呀！{page}
索爾不揭穿法蓮娜，
還放過假冒他的原兇，
不追查背後主使者。{page}
為了什麼？
是為了怕傷害到你！
唉～為了你，{page}
他必須背負著，
縱放原兇的罪名；
還有臣民的不滿，{page}
來自各界的懷疑，
他的犧牲太大了﹒﹒
{speaker char=0}我﹒﹒﹒﹒
我對不起大家～！
這件事，可﹒﹒﹒{page}
可不可以讓我靜一靜，
讓我再想一想？
求求你～！
{speaker char=2}唉～也好！
你先想一想，
再把結果告訴我們。{page}
我先回去營火那裡了，
離開太久，
別人會懷疑﹒﹒﹒﹒
```

### `0x0a`（`ICON19` `0x034`）

```text
{speaker char=0}我﹒﹒對不起大家！
我﹒﹒我該怎麼辦？
法蓮娜不會背叛我，{page}
她的笑容那麼天真，
她是個好女孩！
真的！我相信！
```

### `0x0b`（`ICON19` `0x059`）

```text
{speaker char=1}﹒﹒﹒﹒﹒﹒﹒
```

### `0x0c`（`ICON19` `0x0af`）

```text
{speaker char=123}﹒﹒﹒可憐的孩子，
難過嗎？﹒﹒﹒﹒
{speaker char=0}﹒﹒﹒﹒
我認識妳嗎？
﹒﹒﹒﹒{page}
妳和以前的女武神，
看起來並不太一樣。
{speaker char=123}不錯！
我是最年長的女武神，
我叫凱倫諾特。{page}
﹒﹒﹒孩子，
你很喜歡法蓮娜，
是嗎？
{speaker char=0}我﹒﹒﹒
我相信法蓮娜，
不是這種人﹒﹒﹒
{speaker char=123}但是，
萬一她真的背叛你，
你就無法接受了。{page}
所以你既不願去追查，
也不願面對事實，
很苦惱，是不是？
{speaker char=0}我﹒﹒﹒
我﹒﹒﹒
{speaker char=123}孩子，
只要身是為人類，
就逃避不開命運糾纏{page}
這是永遠擺脫不了的。
孩子，
你需要的是作抉擇。{page}
如果你所擁有的，
是平凡的命運，
那也就算了。{page}
可是你的命運，
卻比尋常人，
還要沉重得多﹒﹒﹒{page}
孩子，你看到了嗎？
你的命運之輪，
已經開始轉動了。{page}
看！你﹑法蓮娜﹑
你的母親﹑所有的人，
都在漩渦當中﹒﹒﹒
{speaker char=0}﹒﹒﹒﹒﹒
告訴我怎麼作，
好嗎？{page}
我已經不想再失去，
身邊最親愛的人了！
{speaker char=123}﹒﹒﹒﹒﹒
抱歉，我不能﹒﹒﹒
你以後會知道﹒﹒﹒{page}
唉～可憐的孩子，
可憐的孩子﹒﹒﹒﹒
```

### `0x0d`（`ICON19` `0x0b9`）

```text
{speaker char=0}妳騙我～！！
妳什麼都不告訴我！
可惡～！{page}
其實妳什麼都知道的，
對不對！﹒﹒
﹒﹒﹒﹒﹒﹒
```

## `FDETXT60`：地圖 59

切到地圖 59 的腳本：`WIN24`（第 25 章勝利，`fdps_chapter_25_end`）。

本區塊另有 9 條有內容、但沒有腳本引用的條目，永遠不會顯示，見 [`cut_content/`](../../cut_content/_index.md)。

### `0x09`（`WIN24` `0x040`）

```text
{speaker char=0}啊～！妳是﹒﹒﹒
{speaker char=121}是的，孩子，
我叫艾芙羅拉，
也是你母親的姐妹。
{speaker char=0}我越來越來不了解，
妳們為什麼要幫助我？
{speaker char=121}用人類的言語解釋，
這就叫作命運吧？
{speaker char=0}我實在不明瞭，
我能為大家做什麼？
我既使再如何努力，{page}
終究還是有人，
會為我哭泣。
我真的不明白﹒﹒﹒﹒
```

### `0x0a`（`WIN24` `0x051`）

```text
{speaker char=121}你後悔了？
```

### `0x0b`（`WIN24` `0x05d`）

```text
{speaker char=0}不，我不後悔！
{speaker char=121}那麼，
就不要再苛責自己了，
沒有人是完美的。{page}
事情都能如願以償，
或許，
人就不需要命運了！{page}
但是，這是辦不到的。
就算是神，也是一樣。
知道嗎？
{speaker char=0}就算是妳也一樣？
{speaker char=121}是的！
{speaker char=0}﹒﹒﹒﹒
{speaker char=121}不要去想太多，孩子，
你這生要做的事太多，
時間卻太短暫了！{page}
來，聽呀！
你聽見風的呼喚嗎？
它正催促你起程呢！
{speaker char=0}什麼？我？
{speaker char=121}傻孩子！
你已經有足夠的智慧，
去了解我的話了。{page}
但是，你的腦海中，
卻有太多回憶，
害怕失去，{page}
害怕遺忘，
所以不敢展開翅膀，
朝向真理之海飛翔。
{speaker char=0}不！我不懂！
我該怎麼作？
妳教我好嗎？
{speaker char=121}傾聽你心中的風聲，
來，放開你的感覺，
將記憶中的臉孔忘去。{page}
孩子，記著！
這個世界是你的，
用自己的眼睛看世界。{page}
孩子，感覺到了嗎？
在這個世界裡，
沒有索爾，沒有我，{page}
沒有法蓮娜，
也沒有尤利安，
和你遇到的朋友。{page}
你在找尋的，
是自己的荊棘之路，
不是別人走過的路。{page}
在這個世界上，
完美，
並不是唯一的標準，{page}
世界上有太多東西，
比完美還要偉大！
去吧！孩子，{page}
那才是你要走的道路，
像風一樣，
知道嗎？
{speaker char=0}要像風一樣﹒﹒﹒﹒
是呀！
我好像有點了解了。
{speaker char=121}是嗎？
那就祝福你，
蘭迪斯，{page}
我們的使命已經結束。
從今而後，
我們要與你永別了。
{speaker char=0}妳們﹒﹒﹒﹒
妳們終於﹒﹒﹒﹒
也要離我而去了嗎？
{speaker char=121}蘭迪斯，再見了。
不要難過，
你的道路雖然遙遠，{page}
但只要有信心，
一步步地走下去，
相信你在未來，{page}
一定也能找到，
屬於自己的幸福。
再見了！{page}
永遠記得身為神族，
你的尊嚴與榮耀，
我們將與你同在﹒﹒
```

### `0x0c`（`WIN24` `0x068`）

```text
{speaker char=0}再見了，
女武神，
我會永遠記得妳的。
```

### `0x0d`（`WIN24` `0x089`）

```text
{speaker char=0}像風一樣﹒﹒﹒﹒
```

### `0x0e`（`WIN24` `0x155`）

```text
{speaker char=0}法蓮娜～！！
```

### `0x0f`（`WIN24` `0x16c`）

```text
{speaker char=1}啊？！蘭迪斯﹒﹒﹒﹒
```

### `0x10`（`WIN24` `0x322`）

```text
{speaker char=0}法蓮娜﹒﹒﹒﹒
```

## `FDETXT61`：地圖 60

切到地圖 60 的腳本：`WIN16`（第 17 章勝利，`fdps_chapter_17_end`）。

本區塊另有 9 條有內容、但沒有腳本引用的條目，永遠不會顯示，見 [`cut_content/`](../../cut_content/_index.md)。

### `0x09`（`WIN16` `0x1da`）

```text
{speaker char=12}你們這些小人！
看我來收拾你們～！！
```

### `0x0a`（`WIN16` `0x22c`）

```text
{speaker char=3}慢﹒﹒﹒﹒
你們跑慢一點～！
我快跟不上了～！！
```

### `0x0b`（`WIN16` `0x241`）

```text
{speaker char=3}喂～！
等等我～！
我真的跑不動了～！
```

### `0x0c`（`WIN16` `0x256`）

```text
{speaker char=3}喂～！
別留下我一個人～！
我會迷路的～！
```

## `FDETXT62`：地圖 61

切到地圖 61 的腳本：`WINGA26`（第 27 章勝利，`fdps_chapter_27_end`）。

本區塊另有 5 條有內容、但沒有腳本引用的條目，永遠不會顯示，見 [`cut_content/`](../../cut_content/_index.md)。

### `0x09`（`WINGA26` `0x3f0`）

```text
{speaker char=1}異界的入口開了！
魔神就要降臨了！
只要在牠來人間前，{page}
先將它擊敗，
我們就有機會，
把牠重新封印了。{page}
時間已經剩下不多！
快！
{speaker char=0}法蓮娜！﹒﹒﹒
```

### `0x0a`（`WINGA26` `0x3ff`）

```text
{speaker char=1}對不起！
因為我父親的任性，
帶給大家很多痛苦，{page}
請讓我盡一份力量，
來阻止魔神的降臨，
彌補父親的罪過！
{speaker char=7}大姐姐！
{speaker char=4}太好了！
我們的人都到齊了！
這是勝利的好預兆！
{speaker char=8}快！
我們趕快行動吧！
```

## `FDETXT64`：地圖 63

切到地圖 63 的腳本：`WIN29`（第 30 章勝利，`fdps_chapter_30_end`）。

本區塊另有 15 條有內容、但沒有腳本引用的條目，永遠不會顯示，見 [`cut_content/`](../../cut_content/_index.md)。

### `0x09`（`WIN29` `0x7a3`）

```text
{speaker char=12}事情終於結束了！
我為你們送行，
也就送到這裡了。{page}
有時間的話，
記得到我那裡作客！
不要忘記了。{page}
蘭斯洛特！珊！
到我那裡去敘敘舊吧！
順便住上一陣子！
{speaker char=11}也好！
如今小伙子都長大了。
該讓他們獨當一面。
{speaker char=10}唉～！
我們都成老骨頭了～！
也該歇歇手了。{page}
走吧！走吧！
別浪費時間了。
我還有尋寶計劃呢！
{speaker char=0}謝謝你們！
我不會忘記你們的。
```

### `0x0a`（`WIN29` `0x7b5`）

```text
{speaker char=7}哇！我怎麼辦～！
大家都要走了。
誰要收留我？嗚嗚～
{speaker char=6}要﹒﹒要不要，
到我那裡去住一陣子？
{speaker char=7}嗚哇！～
我才不要住和尚那裡～
{speaker char=6}唉﹒﹒﹒﹒
{speaker char=3}琴琴，要不要跟我走，
我們倆個在競技場裡，
一定是對無敵搭檔！
{speaker char=7}耶！～
真的呀～！好好玩～！
我去！我要去～！
{speaker char=3}嘻嘻～！
賺死了！賺死了！
這下子撿到寶了！
{speaker char=8}喂！喂！
妳不要教壞小孩子！
{speaker char=3}我知～道啦！
琴琴！
我們走吧！
{speaker char=7}嗨～！我走了！
琴琴要走了！
大哥哥，大姐姐！{page}
你一定要過的幸福喔！
拜拜！
```

### `0x0b`（`WIN29` `0x7c3`）

```text
{speaker char=1}琴琴真是一個好孩子。
希望她忘掉師父的死，
堅強的活下去～！
{speaker char=0}法蓮娜～﹒﹒
{speaker char=8}啊～！
我也要離開了～！
多虧索爾王的幫忙，{page}
我國撤銷我的追捕令，
我又可以回去，
重新我的飛行研究。{page}
蓋亞，
跟我一起回去吧！
我幫你換顆新電池。
{speaker char=4}老伯～！
你別把蓋亞拆了，
拿去做你的實驗喔！
{speaker char=9}嗶嗶嗶嗶～！
{speaker char=8}啊呀呀～！
你這小子真愛胡說。
蓋亞，我們快走吧～！
```

### `0x0c`（`WIN29` `0x7d1`）

```text
{speaker char=2}蘭迪斯，
天下沒有不散的宴席。
我要回西大陸了。{page}
從今之後﹒﹒﹒﹒
{speaker char=0}嗯！我知道，
就靠我自己了。
是不是？
{speaker char=2}嗯，
保重了～！再會！
{speaker char=0}我們還會再見面嗎？
{speaker char=2}﹒﹒﹒﹒﹒
```

### `0x0d`（`WIN29` `0x7db`）

```text
{speaker char=4}就算是道別，
他還是一副酷相！
真拿他沒辦法～！
{speaker char=5}嗯～！大姐姐！
我也想回家了！
妳要記著瑪麗安喔！
{speaker char=1}嗯～！
總有一天，
我們會去找妳的。
{speaker char=5}一定喔～！
一定喔～！
```

### `0x0e`（`WIN29` `0x7e5`）

```text
{speaker char=4}蘭迪斯～！
認識你們真好！
這是我一生裡，{page}
最刺激的冒險了！
記著！我們是兄弟！
有事找我，知道嗎？
{speaker char=0}嗯～！再會了！
我的兄弟！
{speaker char=6}還有﹒﹒還有我，
我家在﹒﹒在﹒﹒
{speaker char=4}哇哈哈哈～！
大教皇，
要找你還不容易嗎？{page}
走吧！走吧！
到我家作客去。
我爺爺急著要看，{page}
當今教皇的樣子呢～！
再見啦！各位！
```

### `0x0f`（`WIN29` `0x817`）

```text
{speaker char=0}大家都走了﹒﹒
```

### `0x10`（`WIN29` `0x824`）

```text
{speaker char=1}希望大家都能，
生活的很快樂﹒﹒﹒
{speaker char=0}咦？
妳為什麼哭了﹒﹒
{speaker char=1}對不起，
看到大家快樂的面容，
我感到很嫉妒。{page}
我的心中，
揮不去父親的陰霾！
對不起，{page}
我對你們說了謊話！
也許我們應該分開，
原諒我好嗎？
{speaker char=0}不﹒﹒不﹒﹒
法蓮娜，不要這樣說！
千萬不要！{page}
我知道，
當我看著妳的眼睛，
我就知道一切了﹒﹒
{speaker char=1}真的？！﹒﹒
{speaker char=0}嗯﹒﹒
所以，不要說抱歉，
好嗎？！﹒﹒
```

### `0x11`（`WIN29` `0x831`）

```text
{speaker char=0}我已經下定決心了；
不管花多少時間，
在我有生之年，{page}
我一定要找到我母親。
{speaker char=1}嗯，
祝你早日實現願望。
{speaker char=0}法蓮娜﹒﹒﹒﹒﹒
{speaker char=1}嗯？
{speaker char=0}我們來作個約定好嗎？
願意嗎？
{speaker char=1}什麼約定？
{speaker char=0}再會的約定！
{speaker char=1}？！﹒﹒﹒
{speaker char=0}不管妳在何處，
不管等多久，答應我！
讓我再見到妳，{page}
那個天真快樂的妳，
好嗎？嗯？
答應我！
{speaker char=1}﹒﹒我答應你﹒﹒﹒
雖然我也不知道，
這需要多少時間。
```

### `0x12`（`WIN29` `0x845`）

```text
{speaker char=0}妳聽！
是風的聲音﹒﹒﹒
{speaker char=1}是的，
我也聽到了！
它在催促你啟程呢！
```

### `0x13`（`WIN29` `0x84e`）

```text
{speaker char=0}妳聽到了！
妳也聽到了！
謝謝妳們，女武神！{page}
我聽到了！
我感覺到了！
```

### `0x14`（`WIN29` `0x871`）

```text
{speaker char=0}再見了，
我會永遠記得妳的。
別讓我久等，好嗎？
```

### `0x15`（`WIN29` `0x883`）

```text
{speaker char=1}等一等！﹒﹒﹒﹒
蘭迪斯！
我要如何去找你？！
```

### `0x16`（`WIN29` `0x88f`）

```text
{speaker char=0}風！﹒﹒﹒﹒
它會告訴妳的！
```

## `FDETXT65`：地圖 64

切到地圖 64 的腳本：`GOODEND`（第 30 章勝利，`fdps_chapter_30_end`）。

本區塊另有 4 條有內容、但沒有腳本引用的條目，永遠不會顯示，見 [`cut_content/`](../../cut_content/_index.md)。

### `0x09`（`GOODEND` `0x0a6`）

```text
{speaker char=81}可惡的小子！
你竟敢壞了我們的事！
你以為你跑得掉嗎？
{speaker char=0}你們這些強盜，
到處為非作歹，
我早想將你除去了！
{speaker char=81}哈哈哈！是嗎？
你確信你打得贏我們？
大家一起上！
```

### `0x0a`（`GOODEND` `0x0b3`）

```text
{speaker char=6}神呀！
總算讓我趕上了！
```

### `0x0b`（`GOODEND` `0x0c4`）

```text
{speaker char=4}一個也別讓他們逃跑！
```

### `0x0c`（`GOODEND` `0x0d5`）

```text
{speaker char=3}哈哈哈！這種貨色！
再來幾個，
結果也是一樣！
```

### `0x0d`（`GOODEND` `0x0ef`）

```text
{speaker char=8}快呀！
再不快點，
敵人就要溜了！
{speaker char=9}嗶﹒﹒﹒
```

### `0x0e`（`GOODEND` `0x111`）

```text
{speaker char=5}有膽就來吃我一箭！
嘻嘻！
```

### `0x0f`（`GOODEND` `0x13f`）

```text
{speaker char=2}真是太危險了！
凡事要考慮清楚！
不要太衝動。
```

### `0x10`（`GOODEND` `0x154`）

```text
{speaker char=7}嗨！大哥哥！
你看看是誰來了～！
```

### `0x11`（`GOODEND` `0x185`）

```text
{speaker char=0}﹒﹒﹒﹒你們？！
﹒﹒法蓮娜？！
{speaker char=1}﹒﹒﹒﹒﹒﹒
```

### `0x12`（`GOODEND` `0x1f7`）

```text
{speaker char=6}前面路途還很遠，
我們要去哪裡？{page}
蘭迪斯，下決定吧！
{speaker char=0}你們大家﹒﹒﹒﹒
```

### `0x13`（`GOODEND` `0x203`）

```text
{speaker char=0}喔～！
我們出發吧～！
```
