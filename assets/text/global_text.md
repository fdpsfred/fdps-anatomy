# 全域文字：`FDETXT00.TXT`

`FIELD.VFS` 的 `FDETXT00.TXT` 是不屬於任何一章的文字，由 `fdps_load_global_resources` 在啟動時以固定檔名載入進 `data_fdps_all_game_text_ptr`，一直留到結束（`src/gamedata.h`）。區塊格式與控制碼見 [`resource_info/text.md`](../../resource_info/text.md)；本頁的原文由 [`tools/global_text/`](../../tools/global_text/_index.md) 以 [`text_decode`](../../tools/text_decode/_index.md) 解出，讀取端由它掃描 `src/` 產生。

共 555 條（`0x000`–`0x22a`）。控制碼照解碼器的寫法：`{br}` 換行、`{page}` 換頁、`{subst1}`／`{subst2}` 代入、`{number}` 數字。

## 讀取方式

讀取端只有兩種取條目的方式，全部經過 `fdps_draw_text`：

- **編號加常數**：名稱表以某個欄位值加上該表的起點當條目。加法不檢查上界，也沒有分表：編號超出一張表的尾端就讀到下一張表的開頭。
- **寫死的條目**：系統訊息由呼叫端直接寫條目編號。

代入碼 `{subst1}`／`{subst2}` 畫的也是本區塊的條目：呼叫端先把「名稱表起點 + 編號」存進 `data_fdps_dialog_last_action_text_id_param`／`data_fdps_dialog_subst_text_id_2`，再畫含代入碼的訊息，`fdps_draw_text` 讀到代入碼時以那個值遞迴畫本區塊的一條，顏色固定用標準訊息色。下表的「代入槽」就是這條路徑。

## 語意分區

| 條目 | 內容 | 取條目的方式 |
| --- | --- | --- |
| `0x000` | 字模列 | 沒有讀取端 |
| `0x001`–`0x096` | 單位名 | 角色編號 + `0x1` |
| `0x097`–`0x09d` | 種族名 | 種族代碼 + `0x97` |
| `0x09e`–`0x0a0` | 空白 | — |
| `0x0a1`–`0x0c8` | 職業名 | 職業代碼 + `0xa1` |
| `0x0c9`–`0x1aa` | 物品名 | 物品編號 + `0xc9` |
| `0x1ab`–`0x1bd` | 空白 | — |
| `0x1be`–`0x1e5` | 法術名 | 法術編號 + `0x1be` |
| `0x1e6`–`0x22a` | 系統訊息 | 寫死的條目，見下方各組 |

章名、章節副標、勝敗條件與城鎮招牌不在本區塊，而在每一章自己的文字區塊開頭，內容由各章頁 [`chapters/`](../../chapters/_index.md) 記。以寫死條目讀章節區塊前 9 條的地方：

| 章節區塊條目 | 讀取端 |
| --- | --- |
| `0x00` | `fdps_chapter_16_event_wandering_smith_forge`（`src/chevt3.c`） |
| `0x01` | `fdps_draw_save_slot_panel`（`src/savepnl.c`） |
| `0x02` | `fdps_battle_show_win_fail_window`（`src/btlend.c`） |
| `0x03` | `fdps_battle_show_win_fail_window`（`src/btlend.c`） |
| `0x04` | `fdps_village_item_menu`（`src/vilmenu.c`） |
| `0x05` | `fdps_run_weapon_shop`（`src/vilshop.c`） |
| `0x06` | `fdps_run_bar_shop`（`src/vilbar.c`） |
| `0x07` | `fdps_run_church_screen`（`src/vilshop.c`） |
| `0x08` | `fdps_chapter_30_event_deploy_wave_4`（`src/chevt6.c`）、`fdps_run_secret_menu`（`src/vilshop.c`） |

## `0x000`：字模列

第 0 條是一整列數字與大寫字母。沒有任何讀取端：名稱表的起點都大於 0，系統訊息也沒有寫死 0。它是永遠不會顯示的文字，內容由 [`cut_content/`](../../cut_content/_index.md) 收錄。

## `0x001`–`0x096`：單位名

單位的名稱是本區塊第「角色編號 + 1」條。角色編號就是肖像編號：`fdps_deploy_unit`（`src/deploy.c`）部署時與 `fdps_roster_add_character`（`src/roster.c`）入隊時，都把同一個值同時寫進單位記錄的 `portrait_id` 與 `char_id`，所以讀取端取哪一個結果都一樣——敵方單位的名稱因此是「肖像編號 + 1」，也就是 `ENEMYDAT.DAT` 第 r 列（肖像編號 `0x3C` + r）的名稱在第 `0x3D` + r 條。

本表延伸到 `0x096` 為止，後面緊接種族名。部署記錄用到的肖像編號 `0x97`–`0x9C`（見 [`assets/enemies.md`](../enemies.md)）照公式落在 `0x098`–`0x09d`，讀到的是種族名「妖鬼」到「其他」。

一個編號的名稱會不會真的出現在畫面上，取決於那個單位有沒有出場；從未出場的單位由 [`cut_content/`](../../cut_content/_index.md) 擁有。

讀取端：

- 直接繪製：
  - `fdps_church_select_promote_candidate`（`src/church.c`）以 `member->char_id`
  - `fdps_roster_revive_fallen_members`（`src/roster.c`）以 `member->char_id`
  - `fdps_shop_draw_member_entry`（`src/shopdraw.c`）以 `(int) member->char_id`
  - `fdps_draw_unit_status_panel`（`src/statunit.c`）以 `char_id`
  - `fdps_village_select_member`（`src/vilmenu.c`）以 `member->char_id`
- 存進代入槽一，由訊息的 `{subst1}` 繪製：
  - `fdps_church_promote_loop`（`src/church.c`）以 `(int) member->portrait_id`
  - `fdps_shop_buy_loop`（`src/shop.c`）以 `(int) member_record->char_id`
  - `fdps_village_item_sell_loop`（`src/vilmenu.c`）以 `member->char_id`
  - `fdps_village_item_transfer_loop`（`src/vilmenu.c`）以 `giver->char_id`
  - `fdps_village_member_equip_loop`（`src/vilmenu.c`）以 `member->char_id`

| 條目 | 角色編號 | 原文 |
| --- | --- | --- |
| `0x001` | `0x00` | 蘭迪斯 |
| `0x002` | `0x01` | 法蓮娜 |
| `0x003` | `0x02` | 費塔加 |
| `0x004` | `0x03` | 裘娜 |
| `0x005` | `0x04` | 亞克 |
| `0x006` | `0x05` | 瑪麗安 |
| `0x007` | `0x06` | 尤利安 |
| `0x008` | `0x07` | 琴琴 |
| `0x009` | `0x08` | 布蘭多 |
| `0x00a` | `0x09` | 蓋亞 |
| `0x00b` | `0x0a` | 珊 |
| `0x00c` | `0x0b` | 蘭斯洛特 |
| `0x00d` | `0x0c` | 索爾 |
| `0x00e` | `0x0d` | 卡里斯 |
| `0x00f` | `0x0e` | 亞雷斯 |
| `0x010`–`0x023` | `0x0f`–`0x22` | （空字串） |
| `0x024` | `0x23` | ？？？？ |
| `0x025`–`0x03b` | `0x24`–`0x3a` | （空字串） |
| `0x03c` | `0x3b` | 侍衛 |
| `0x03d` | `0x3c` | 平衡之神 |
| `0x03e` | `0x3d` | 平衡之神 |
| `0x03f` | `0x3e` | 平衡之神 |
| `0x040` | `0x3f` | 魔導王吉歐 |
| `0x041` | `0x40` | 塞克斯 |
| `0x042` | `0x41` | 布魯森 |
| `0x043` | `0x42` | 汎拉沛 |
| `0x044` | `0x43` | 凱因巴 |
| `0x045` | `0x44` | 薩達特 |
| `0x046` | `0x45` | 巴魯 |
| `0x047` | `0x46` | 席拉 |
| `0x048` | `0x47` | 石巨神 |
| `0x049` | `0x48` | 死神 |
| `0x04a` | `0x49` | 巫湯婆婆 |
| `0x04b` | `0x4a` | 蛇魔女 |
| `0x04c` | `0x4b` | 蛇魔使 |
| `0x04d` | `0x4c` | 暗黑騎兵 |
| `0x04e` | `0x4d` | 暗黑騎士 |
| `0x04f` | `0x4e` | 地獄騎士 |
| `0x050` | `0x4f` | 野武士 |
| `0x051` | `0x50` | 野蠻戰士 |
| `0x052` | `0x51` | 狂戰士 |
| `0x053` | `0x52` | 狼人 |
| `0x054` | `0x53` | 狼人戰士 |
| `0x055` | `0x54` | 骷顱兵 |
| `0x056` | `0x55` | 白骨戰士 |
| `0x057` | `0x56` | 傭兵 |
| `0x058` | `0x57` | 傭兵戰士 |
| `0x059` | `0x58` | 騎兵 |
| `0x05a` | `0x59` | 騎士 |
| `0x05b` | `0x5a` | 衛兵 |
| `0x05c` | `0x5b` | 侍衛 |
| `0x05d` | `0x5c` | 禁衛隊 |
| `0x05e` | `0x5d` | 弓兵 |
| `0x05f` | `0x5e` | 弓箭手 |
| `0x060` | `0x5f` | 神箭手 |
| `0x061` | `0x60` | 飛兵 |
| `0x062` | `0x61` | 天空騎士 |
| `0x063` | `0x62` | 步兵 |
| `0x064` | `0x63` | 武士 |
| `0x065` | `0x64` | 鎧甲武士 |
| `0x066` | `0x65` | 冰魔導士 |
| `0x067` | `0x66` | 魔導士 |
| `0x068` | `0x67` | 暗魔導士 |
| `0x069` | `0x68` | 黑暗祭司 |
| `0x06a` | `0x69` | 幽魂 |
| `0x06b` | `0x6a` | 死靈 |
| `0x06c` | `0x6b` | 地獄犬 |
| `0x06d` | `0x6c` | 拳士 |
| `0x06e` | `0x6d` | 武鬥家 |
| `0x06f` | `0x6e` | 寶箱怪 |
| `0x070` | `0x6f` | 寶箱妖精 |
| `0x071` | `0x70` | 守護魔龍 |
| `0x072` | `0x71` | 修佩魯 |
| `0x073` | `0x72` | 雷德 |
| `0x074` | `0x73` | 亞德尼恩 |
| `0x075` | `0x74` | 裘娜 |
| `0x076` | `0x75` | 假索爾 |
| `0x077` | `0x76` | 村民 |
| `0x078` | `0x77` | 村婦 |
| `0x079` | `0x78` | 村長 |
| `0x07a` | `0x79` | 艾芙羅拉 |
| `0x07b` | `0x7a` | 絲卡蒂亞 |
| `0x07c` | `0x7b` | 凱倫諾特 |
| `0x07d` | `0x7c` | 大祭司 |
| `0x07e` | `0x7d` | 瑪茜 |
| `0x07f` | `0x7e` | 巴特 |
| `0x080` | `0x7f` | 火神 |
| `0x081` | `0x80` | 光束砲座 |
| `0x082`–`0x096` | `0x81`–`0x95` | （空字串） |

## `0x097`–`0x09d`：種族名

種族代碼 0–6 各有名稱，後面三條是空字串。

讀取端：

- 直接繪製：
  - `fdps_draw_unit_status_panel`（`src/statunit.c`）以 `race`

| 條目 | 種族代碼 | 原文 |
| --- | --- | --- |
| `0x097` | `0x00` | 人類 |
| `0x098` | `0x01` | 妖鬼 |
| `0x099` | `0x02` | 魔族 |
| `0x09a` | `0x03` | 機械 |
| `0x09b` | `0x04` | 獸人 |
| `0x09c` | `0x05` | 龍族 |
| `0x09d` | `0x06` | 其他 |

## `0x0a1`–`0x0c8`：職業名

職業代碼 `0x00`–`0x27` 共 40 個。

讀取端：

- 直接繪製：
  - `fdps_church_select_promote_candidate`（`src/church.c`）以 `target_class_id`
  - `fdps_draw_unit_status_panel`（`src/statunit.c`）以 `clazz`
- 存進代入槽二，由訊息的 `{subst2}` 繪製：
  - `fdps_church_promote_loop`（`src/church.c`）以 `(int) promotion_route[PROMOTION_ROUTE_CLASS]`

| 條目 | 職業代碼 | 原文 |
| --- | --- | --- |
| `0x0a1` | `0x00` | 劍士 |
| `0x0a2` | `0x01` | 劍聖 |
| `0x0a3` | `0x02` | 劍帝 |
| `0x0a4` | `0x03` | 英雄 |
| `0x0a5` | `0x04` | 戰士 |
| `0x0a6` | `0x05` | 聖戰士 |
| `0x0a7` | `0x06` | 狂戰士 |
| `0x0a8` | `0x07` | 騎士 |
| `0x0a9` | `0x08` | 聖騎士 |
| `0x0aa` | `0x09` | 大地騎士 |
| `0x0ab` | `0x0a` | 弓兵 |
| `0x0ac` | `0x0b` | 神箭手 |
| `0x0ad` | `0x0c` | 狙擊王 |
| `0x0ae` | `0x0d` | 魔導士 |
| `0x0af` | `0x0e` | 法師 |
| `0x0b0` | `0x0f` | 巫師 |
| `0x0b1` | `0x10` | 僧侶 |
| `0x0b2` | `0x11` | 大僧侶 |
| `0x0b3` | `0x12` | 大祭司 |
| `0x0b4` | `0x13` | 武道家 |
| `0x0b5` | `0x14` | 武聖 |
| `0x0b6` | `0x15` | 武神 |
| `0x0b7` | `0x16` | 技師 |
| `0x0b8` | `0x17` | 機械伯爵 |
| `0x0b9` | `0x18` | 機械大師 |
| `0x0ba` | `0x19` | 機兵 |
| `0x0bb` | `0x1a` | 魔神 |
| `0x0bc` | `0x1b` | 魔導王 |
| `0x0bd` | `0x1c` | 魔導戰士 |
| `0x0be` | `0x1d` | 武士 |
| `0x0bf` | `0x1e` | 重裝武士 |
| `0x0c0` | `0x1f` | 飛兵 |
| `0x0c1` | `0x20` | 妖魔 |
| `0x0c2` | `0x21` | 守護獸 |
| `0x0c3` | `0x22` | 將軍 |
| `0x0c4` | `0x23` | 傭兵 |
| `0x0c5` | `0x24` | ？？ |
| `0x0c6` | `0x25` | 惡靈 |
| `0x0c7` | `0x26` | 活屍 |
| `0x0c8` | `0x27` | （空字串） |

## `0x0c9`–`0x1aa`：物品名

物品編號 `0x00`–`0xE1` 共 226 個，與 `ITEM.DAT` 有內容的範圍相同（[`assets/items.md`](../items.md)）。表尾之後是 19 條空字串，再來就是法術名：照公式，物品編號 `0xE2`–`0xF4` 的名稱是空字串，`0xF5` 起讀到法術名，物品編號 `0xFF` 讀到第 `0x1c8` 條、法術 `0x0A` 的「裂地術」——攻略站把編號 `FF` 的 BUG 物品叫做「裂地術」，來源就是這個越界。

讀取端：

- 直接繪製：
  - `fdps_shop_draw_item_entry`（`src/shopdraw.c`）以 `item_id`
  - `fdps_draw_unit_inventory`（`src/statunit.c`）以 `item_id`
- 存進代入槽一，由訊息的 `{subst1}` 繪製：
  - `fdps_battle_search_cell_at_cursor`（`src/btlact.c`）以 `cell_payload`
  - `fdps_run_death_scripts`（`src/death.c`）以 `operand`
  - `fdps_village_item_sell_loop`（`src/vilmenu.c`）以 `item_id`
- 存進代入槽二，由訊息的 `{subst2}` 繪製：
  - `fdps_battle_search_cell_at_cursor`（`src/btlact.c`）以 `offered_item_id`
  - `fdps_shop_buy_loop`（`src/shop.c`）以 `equipped_item_id`
  - `fdps_village_item_transfer_loop`（`src/vilmenu.c`）以 `item_id`

| 條目 | 物品編號 | 原文 |
| --- | --- | --- |
| `0x0c9` | `0x00` | 岩石 |
| `0x0ca` | `0x01` | 鐵劍 |
| `0x0cb` | `0x02` | 長劍 |
| `0x0cc` | `0x03` | 白刃劍 |
| `0x0cd` | `0x04` | 巨劍 |
| `0x0ce` | `0x05` | 寬刃劍 |
| `0x0cf` | `0x06` | 護手劍 |
| `0x0d0` | `0x07` | 鋸齒劍 |
| `0x0d1` | `0x08` | 邪神劍 |
| `0x0d2` | `0x09` | 雷神之劍 |
| `0x0d3` | `0x0a` | 寒冰劍 |
| `0x0d4` | `0x0b` | 聖火之劍 |
| `0x0d5` | `0x0c` | 大地之劍 |
| `0x0d6` | `0x0d` | 天空之劍 |
| `0x0d7` | `0x0e` | 精靈之劍 |
| `0x0d8` | `0x0f` | 鐵刀 |
| `0x0d9` | `0x10` | 彎月刀 |
| `0x0da` | `0x11` | 軍刀 |
| `0x0db` | `0x12` | 淬毒爪 |
| `0x0dc` | `0x13` | 雙刃刀 |
| `0x0dd` | `0x14` | 地獄刀 |
| `0x0de` | `0x15` | 大斬馬刀 |
| `0x0df` | `0x16` | 龍牙刀 |
| `0x0e0` | `0x17` | 日輪刀 |
| `0x0e1` | `0x18` | 水晶刀 |
| `0x0e2` | `0x19` | 破魔刀 |
| `0x0e3` | `0x1a` | 閃光指環 |
| `0x0e4` | `0x1b` | 中子砲 |
| `0x0e5` | `0x1c` | 女神之杖 |
| `0x0e6` | `0x1d` | 牙 |
| `0x0e7` | `0x1e` | 長矛 |
| `0x0e8` | `0x1f` | 騎士槍 |
| `0x0e9` | `0x20` | 戰矛 |
| `0x0ea` | `0x21` | 衝鋒之矛 |
| `0x0eb` | `0x22` | 白銀之槍 |
| `0x0ec` | `0x23` | 雷之槍 |
| `0x0ed` | `0x24` | 神光之矛 |
| `0x0ee` | `0x25` | 龍翼之矛 |
| `0x0ef` | `0x26` | 寶石之槍 |
| `0x0f0` | `0x27` | 鋼矛 |
| `0x0f1` | `0x28` | 極光之矛 |
| `0x0f2` | `0x29` | 破陣之矛 |
| `0x0f3` | `0x2a` | 聖光之矛 |
| `0x0f4` | `0x2b` | 黃金之矛 |
| `0x0f5` | `0x2c` | 封魔之矛 |
| `0x0f6` | `0x2d` | 修羅之矛 |
| `0x0f7` | `0x2e` | 檜木杖 |
| `0x0f8` | `0x2f` | 魔法之杖 |
| `0x0f9` | `0x30` | 封咒之杖 |
| `0x0fa` | `0x31` | 黑暗之杖 |
| `0x0fb` | `0x32` | 日輪之杖 |
| `0x0fc` | `0x33` | 光之杖 |
| `0x0fd` | `0x34` | 天使之杖 |
| `0x0fe` | `0x35` | 梅林之杖 |
| `0x0ff` | `0x36` | 龍牙杖 |
| `0x100` | `0x37` | 鑽石之杖 |
| `0x101` | `0x38` | 銅指環 |
| `0x102` | `0x39` | 刺針指環 |
| `0x103` | `0x3a` | 白銀指環 |
| `0x104` | `0x3b` | 精鋼指環 |
| `0x105` | `0x3c` | 淬毒指環 |
| `0x106` | `0x3d` | 皇帝指環 |
| `0x107` | `0x3e` | 詛咒指環 |
| `0x108` | `0x3f` | 力量拳套 |
| `0x109` | `0x40` | 精鋼手套 |
| `0x10a` | `0x41` | 魔力拳套 |
| `0x10b` | `0x42` | 烈神拳套 |
| `0x10c` | `0x43` | 封咒手套 |
| `0x10d` | `0x44` | 迷蹤手套 |
| `0x10e` | `0x45` | 短弓 |
| `0x10f` | `0x46` | 長弓 |
| `0x110` | `0x47` | 狙擊弓 |
| `0x111` | `0x48` | 十字弓 |
| `0x112` | `0x49` | 精靈弓 |
| `0x113` | `0x4a` | 風神弓 |
| `0x114` | `0x4b` | 妖刀 |
| `0x115` | `0x4c` | 水晶之弓 |
| `0x116` | `0x4d` | 黃金之弓 |
| `0x117` | `0x4e` | 鐵珠砲 |
| `0x118` | `0x4f` | 弩砲 |
| `0x119` | `0x50` | 火神砲 |
| `0x11a` | `0x51` | 電光砲 |
| `0x11b` | `0x52` | 閃光砲 |
| `0x11c` | `0x53` | 激光砲 |
| `0x11d` | `0x54` | 雷光砲 |
| `0x11e` | `0x55` | 極光砲 |
| `0x11f` | `0x56` | 重光束砲 |
| `0x120` | `0x57` | 粒子砲 |
| `0x121` | `0x58` | 修佩魯 |
| `0x122` | `0x59` | 雷德 |
| `0x123` | `0x5a` | 亞德尼恩 |
| `0x124` | `0x5b` | 鐮刀 |
| `0x125` | `0x5c` | 地獄之門 |
| `0x126` | `0x5d` | 爪 |
| `0x127` | `0x5e` | 斧 |
| `0x128` | `0x5f` | 靈擊 |
| `0x129` | `0x60` | 水晶球 |
| `0x12a` | `0x61` | 武士刀 |
| `0x12b` | `0x62` | 炎龍劍 |
| `0x12c` | `0x63` | 光束砲 |
| `0x12d` | `0x64` | 銅鎧甲 |
| `0x12e` | `0x65` | 白銀鎧甲 |
| `0x12f` | `0x66` | 青鎧甲 |
| `0x130` | `0x67` | 魔法鎧甲 |
| `0x131` | `0x68` | 龍鱗鎧甲 |
| `0x132` | `0x69` | 重鎧甲 |
| `0x133` | `0x6a` | 精鋼鎧甲 |
| `0x134` | `0x6b` | 雷神鎧甲 |
| `0x135` | `0x6c` | 聖之鎧甲 |
| `0x136` | `0x6d` | 地獄鎧甲 |
| `0x137` | `0x6e` | 大地鎧甲 |
| `0x138` | `0x6f` | 惡魔鎧甲 |
| `0x139` | `0x70` | 精靈鎧甲 |
| `0x13a` | `0x71` | 布衣 |
| `0x13b` | `0x72` | 旅行衣 |
| `0x13c` | `0x73` | 元素服 |
| `0x13d` | `0x74` | 硬皮甲 |
| `0x13e` | `0x75` | 浸漬皮甲 |
| `0x13f` | `0x76` | 符咒皮甲 |
| `0x140` | `0x77` | 白銀鍊甲 |
| `0x141` | `0x78` | 合金鎖甲 |
| `0x142` | `0x79` | 銀鱗甲 |
| `0x143` | `0x7a` | 精鋼鱗甲 |
| `0x144` | `0x7b` | 破魔甲 |
| `0x145` | `0x7c` | 聖紋鎖甲 |
| `0x146` | `0x7d` | 闇魔環甲 |
| `0x147` | `0x7e` | 血銅鍊甲 |
| `0x148` | `0x7f` | 赤鍊蛇甲 |
| `0x149` | `0x80` | 黃金鎖甲 |
| `0x14a` | `0x81` | 布袍 |
| `0x14b` | `0x82` | 法師長袍 |
| `0x14c` | `0x83` | 僧侶袍 |
| `0x14d` | `0x84` | 祭司袍 |
| `0x14e` | `0x85` | 賢者之袍 |
| `0x14f` | `0x86` | 星月之袍 |
| `0x150` | `0x87` | 虛無之袍 |
| `0x151` | `0x88` | 天空披風 |
| `0x152` | `0x89` | 聖袍 |
| `0x153` | `0x8a` | 惡神之袍 |
| `0x154` | `0x8b` | 詛咒之袍 |
| `0x155` | `0x8c` | 金縷袍 |
| `0x156` | `0x8d` | 國王新衣 |
| `0x157` | `0x8e` | 武道服 |
| `0x158` | `0x8f` | 鏽龍道服 |
| `0x159` | `0x90` | 鳳舞戰服 |
| `0x15a` | `0x91` | 帝王道服 |
| `0x15b` | `0x92` | 九陽道服 |
| `0x15c` | `0x93` | 天道戰服 |
| `0x15d` | `0x94` | 龍威戰服 |
| `0x15e` | `0x95` | 聖武道服 |
| `0x15f` | `0x96` | 鬼面道服 |
| `0x160` | `0x97` | 混沌道服 |
| `0x161` | `0x98` | 金絲道服 |
| `0x162` | `0x99` | 硬鐵裝甲 |
| `0x163` | `0x9a` | 複式裝甲 |
| `0x164` | `0x9b` | 強化裝甲 |
| `0x165` | `0x9c` | 特殊裝甲 |
| `0x166` | `0x9d` | 黃金裝甲 |
| `0x167` | `0x9e` | 神聖裝甲 |
| `0x168` | `0x9f` | 岩石裝甲 |
| `0x169` | `0xa0` | 灼烈之劍 |
| `0x16a` | `0xa1` | 火光之劍 |
| `0x16b` | `0xa2` | 真炎龍劍 |
| `0x16c` | `0xa3` | 金屬礦 |
| `0x16d` | `0xa4` | 強化套件 |
| `0x16e` | `0xa5` | 妖刀村雨 |
| `0x16f` | `0xa6` | 妖刀村正 |
| `0x170` | `0xa7` | 妖刀正宗 |
| `0x171` | `0xa8` | 皇家聖弓 |
| `0x172` | `0xa9` | 神的聖印 |
| `0x173` | `0xaa` | 猛毒爪 |
| `0x174` | `0xab` | 使魔 |
| `0x175` | `0xac` | 古代砲 |
| `0x176` | `0xad` | 強化套件 |
| `0x177` | `0xae` | 靈體護壁 |
| `0x178` | `0xaf` | 劍牙 |
| `0x179` | `0xb0` | 硬皮 |
| `0x17a` | `0xb1` | 形見指環 |
| `0x17b` | `0xb2` | 地獄弓 |
| `0x17c` | `0xb3` | 魔精石碎片 |
| `0x17d` | `0xb4` | 藥草 |
| `0x17e` | `0xb5` | 回復劑 |
| `0x17f` | `0xb6` | 再生藥 |
| `0x180` | `0xb7` | 魔法草 |
| `0x181` | `0xb8` | 魔法水 |
| `0x182` | `0xb9` | 水晶粒 |
| `0x183` | `0xba` | 死神契約 |
| `0x184` | `0xbb` | 高能量裝置 |
| `0x185` | `0xbc` | 邪神戰斧 |
| `0x186` | `0xbd` | 魔神杖 |
| `0x187` | `0xbe` | 高能量砲 |
| `0x188` | `0xbf` | 斬鐵劍 |
| `0x189` | `0xc0` | 斬鐵劍 |
| `0x18a` | `0xc1` | 斬鐵劍 |
| `0x18b` | `0xc2` | 神聖之水 |
| `0x18c` | `0xc3` | 炎之珠 |
| `0x18d` | `0xc4` | 雷之珠 |
| `0x18e` | `0xc5` | 冰之珠 |
| `0x18f` | `0xc6` | 地之珠 |
| `0x190` | `0xc7` | 火焰 |
| `0x191` | `0xc8` | 炎之寶石 |
| `0x192` | `0xc9` | 雷之寶石 |
| `0x193` | `0xca` | 冰之寶石 |
| `0x194` | `0xcb` | 地之寶石 |
| `0x195` | `0xcc` | （空字串） |
| `0x196` | `0xcd` | 炎之水晶 |
| `0x197` | `0xce` | 雷之水晶 |
| `0x198` | `0xcf` | 冰之水晶 |
| `0x199` | `0xd0` | 光之水晶 |
| `0x19a` | `0xd1` | 魔神護壁 |
| `0x19b` | `0xd2` | 炎之魔石 |
| `0x19c` | `0xd3` | 雷之魔石 |
| `0x19d` | `0xd4` | 冰之魔石 |
| `0x19e` | `0xd5` | 空之寶石 |
| `0x19f` | `0xd6` | 生命之實 |
| `0x1a0` | `0xd7` | 魔力水晶 |
| `0x1a1` | `0xd8` | 力量藥水 |
| `0x1a2` | `0xd9` | 耐力藥水 |
| `0x1a3` | `0xda` | 速度藥水 |
| `0x1a4` | `0xdb` | 勇者徽章 |
| `0x1a5` | `0xdc` | 反禁制器 |
| `0x1a6` | `0xdd` | 風精之羽 |
| `0x1a7` | `0xde` | 解毒劑 |
| `0x1a8` | `0xdf` | 退麻劑 |
| `0x1a9` | `0xe0` | 光之徽章 |
| `0x1aa` | `0xe1` | 暗之徽章 |

## `0x1be`–`0x1e5`：法術名

法術編號 `0x00`–`0x27` 共 40 個，與 `MAGICDAT.DAT` 相同（[`assets/spells.md`](../spells.md)）。

讀取端：

- 直接繪製：
  - `fdps_draw_spell_list_page`（`src/spellmnu.c`）以 `spell_id`
  - `fdps_unit_award_exp_and_level_up`（`src/unitstat.c`）以 `spell_id`

| 條目 | 法術編號 | 原文 |
| --- | --- | --- |
| `0x1be` | `0x00` | 業火 |
| `0x1bf` | `0x01` | 狂暴巨燄 |
| `0x1c0` | `0x02` | 烈獄之火 |
| `0x1c1` | `0x03` | 光之箭 |
| `0x1c2` | `0x04` | 聖光柱 |
| `0x1c3` | `0x05` | 落雷術 |
| `0x1c4` | `0x06` | 奔雷彈 |
| `0x1c5` | `0x07` | 暴雷絕擊 |
| `0x1c6` | `0x08` | 冰爆術 |
| `0x1c7` | `0x09` | 絕殺冰封 |
| `0x1c8` | `0x0a` | 裂地術 |
| `0x1c9` | `0x0b` | 封神裂震 |
| `0x1ca` | `0x0c` | 震空重力彈 |
| `0x1cb` | `0x0d` | 鬼動死靈陣 |
| `0x1cc` | `0x0e` | 恢復之光 |
| `0x1cd` | `0x0f` | 治癒之風 |
| `0x1ce` | `0x10` | 痊癒之泉 |
| `0x1cf` | `0x11` | 封魔咒術 |
| `0x1d0` | `0x12` | 腐毒術 |
| `0x1d1` | `0x13` | 麻痺術 |
| `0x1d2` | `0x14` | 神之祝福 |
| `0x1d3` | `0x15` | 傳送術 |
| `0x1d4` | `0x16` | 神行術 |
| `0x1d5` | `0x17` | 咒殺術 |
| `0x1d6` | `0x18` | 甦癒術 |
| `0x1d7` | `0x19` | 魔龍霸炎 |
| `0x1d8` | `0x1a` | 強力衝擊 |
| `0x1d9` | `0x1b` | 金剛斬 |
| `0x1da` | `0x1c` | 獄炎烈破彈 |
| `0x1db` | `0x1d` | 轟神砲 |
| `0x1dc` | `0x1e` | 流星箭 |
| `0x1dd` | `0x1f` | 靈彈超必殺 |
| `0x1de` | `0x20` | 審判之雷 |
| `0x1df` | `0x21` | 鎮魂之歌 |
| `0x1e0` | `0x22` | 超重力黑洞 |
| `0x1e1` | `0x23` | 聖龍烈霸斬 |
| `0x1e2` | `0x24` | 爆炎狂龍 |
| `0x1e3` | `0x25` | 冰魔超速彈 |
| `0x1e4` | `0x26` | 極度冰凍 |
| `0x1e5` | `0x27` | 萬神降臨 |

## `0x1e6`–`0x22a`：系統訊息

每一條都有寫死它的讀取端。`0x224`–`0x229` 的抽獎只在系統日期是 1998 年 1 月 28 日時開（`fdps_run_bonus_lottery`，`src/vilbar.c`），大獎是被封住的內容，見 [`cut_content/`](../../cut_content/_index.md)。

### `0x1e6`–`0x1e9`：敵兵死亡時的掉落

| 條目 | 原文 | 讀取端 |
| --- | --- | --- |
| `0x1e6` | 撿到{subst1}！！ | `fdps_run_death_scripts`（`src/death.c`） |
| `0x1e7` | 道具滿了，要丟棄嗎？ | `fdps_run_death_scripts`（`src/death.c`） |
| `0x1e8` | 那麼就把{subst1}{br}丟掉吧！！ | `fdps_run_death_scripts`（`src/death.c`） |
| `0x1e9` | 撿到{number}元！！ | `fdps_run_death_scripts`（`src/death.c`） |

### `0x1ea`–`0x1ef`：戰場系統選單：全軍行動與結束回合

| 條目 | 原文 | 讀取端 |
| --- | --- | --- |
| `0x1ea` | 確定要行軍嗎？ | `fdps_battle_system_menu`（`src/btlmenu.c`） |
| `0x1eb` | 那麼就開始行軍吧！ | `fdps_battle_system_menu`（`src/btlmenu.c`） |
| `0x1ec` | 那麼就放棄行軍吧！ | `fdps_battle_system_menu`（`src/btlmenu.c`） |
| `0x1ed` | 確定要結束本回合的行{br}動嗎？ | `fdps_battle_system_menu`（`src/btlmenu.c`） |
| `0x1ee` | 那麼就結束行動吧！ | `fdps_battle_system_menu`（`src/btlmenu.c`） |
| `0x1ef` | 是嗎？那就不要了！ | `fdps_battle_system_menu`（`src/btlmenu.c`） |

### `0x1f0`–`0x1f3`：離開遊戲與記錄戰況

| 條目 | 原文 | 讀取端 |
| --- | --- | --- |
| `0x1f0` | 確定要離開遊戲嗎？ | `fdps_battle_system_submenu`（`src/btlmenu.c`）、`fdps_run_bar_shop`（`src/vilbar.c`） |
| `0x1f1` | 那麼請休息吧！ | `fdps_battle_system_submenu`（`src/btlmenu.c`）、`fdps_run_bar_shop`（`src/vilbar.c`） |
| `0x1f2` | 那麼就繼續吧！ | `fdps_battle_system_submenu`（`src/btlmenu.c`） |
| `0x1f3` | 要記錄目前的戰況嗎？ | `fdps_battle_system_submenu`（`src/btlmenu.c`） |

### `0x1f4`–`0x202`：城鎮：道具、商店、酒店、教會與秘密商店

| 條目 | 原文 | 讀取端 |
| --- | --- | --- |
| `0x1f4` | 歡迎光臨，需要什麼嗎？ | `fdps_village_item_menu`（`src/vilmenu.c`） |
| `0x1f5` | 還需要什麼嗎？ | `fdps_village_item_menu`（`src/vilmenu.c`）、`fdps_run_secret_menu`（`src/vilshop.c`） |
| `0x1f6` | {subst1}要把裝備中的{br}{subst2}賣掉嗎？ | `fdps_shop_buy_loop`（`src/shop.c`） |
| `0x1f7` | 無法裝備 | `fdps_shop_draw_member_entry`（`src/shopdraw.c`） |
| `0x1f8` | 您要付{number}元，接受嗎？ | `fdps_shop_buy_loop`（`src/shop.c`） |
| `0x1f9` | 我還要付您{number}元，接受嗎？ | `fdps_shop_buy_loop`（`src/shop.c`） |
| `0x1fa` | 道具滿了，帶不下！{page} | `fdps_shop_buy_loop`（`src/shop.c`）、`fdps_village_item_transfer_loop`（`src/vilmenu.c`） |
| `0x1fb` | {subst1}身上沒有攜帶任何道具！{page} | `fdps_village_item_sell_loop`（`src/vilmenu.c`）、`fdps_village_item_transfer_loop`（`src/vilmenu.c`）、`fdps_village_member_equip_loop`（`src/vilmenu.c`） |
| `0x1fc` | {subst1}價值{number}元，接受嗎？ | `fdps_village_item_sell_loop`（`src/vilmenu.c`） |
| `0x1fd` | {subst1}的{subst2}要交給誰？{page} | `fdps_village_item_transfer_loop`（`src/vilmenu.c`） |
| `0x1fe` | 呃，要什麼？ | `fdps_run_weapon_shop`（`src/vilshop.c`） |
| `0x1ff` | 還要什麼？ | `fdps_run_weapon_shop`（`src/vilshop.c`） |
| `0x200` | 需要我的幫忙嗎？ | `fdps_run_bar_shop`（`src/vilbar.c`）、`fdps_run_church_screen`（`src/vilshop.c`） |
| `0x201` | 還有什麼事嗎？ | `fdps_run_bar_shop`（`src/vilbar.c`）、`fdps_run_church_screen`（`src/vilshop.c`） |
| `0x202` | 歡迎來到秘密的商店，需要什麼嗎？ | `fdps_run_secret_menu`（`src/vilshop.c`） |

### `0x203`–`0x209`：記錄與讀取

| 條目 | 原文 | 讀取端 |
| --- | --- | --- |
| `0x203` | 戰況確實的被記錄了。 | `fdps_battle_system_submenu`（`src/btlmenu.c`） |
| `0x204` | 那麼就放棄記錄吧！ | `fdps_battle_system_submenu`（`src/btlmenu.c`） |
| `0x205` | 要讀取先前的記錄嗎？ | `fdps_battle_system_submenu`（`src/btlmenu.c`） |
| `0x206` | 那麼就讀取記錄吧！ | `fdps_battle_system_submenu`（`src/btlmenu.c`） |
| `0x207` | 那麼就放棄讀取吧！ | `fdps_battle_system_submenu`（`src/btlmenu.c`） |
| `0x208` | 不正確的記錄！{page} | `fdps_load_savegame`（`src/savefile.c`） |
| `0x209` | 無儲存記錄！ | `fdps_draw_save_slot_panel`（`src/savepnl.c`） |

### `0x20a`–`0x213`：搜尋寶箱與寶物、錢不夠

| 條目 | 原文 | 讀取端 |
| --- | --- | --- |
| `0x20a` | 發現寶箱，要打開嗎？ | `fdps_battle_search_cell_at_cursor`（`src/btlact.c`） |
| `0x20b` | 發現寶物，要挖掘嗎？ | `fdps_battle_search_cell_at_cursor`（`src/btlact.c`） |
| `0x20c` | 那麼就放棄吧！{page} | `fdps_battle_search_cell_at_cursor`（`src/btlact.c`） |
| `0x20d` | 發現{subst1}！{page} | `fdps_battle_search_cell_at_cursor`（`src/btlact.c`） |
| `0x20e` | 帶不下了，要與身上的{br}物品交換嗎？ | `fdps_battle_search_cell_at_cursor`（`src/btlact.c`） |
| `0x20f` | 拿起了{subst1}，{br}把{subst2}放了回去！{page} | `fdps_battle_search_cell_at_cursor`（`src/btlact.c`） |
| `0x210` | 那就把{subst1}{br}放回去吧！{page} | `fdps_battle_search_cell_at_cursor`（`src/btlact.c`） |
| `0x211` | 裡面是空的！{page} | `fdps_battle_search_cell_at_cursor`（`src/btlact.c`） |
| `0x212` | 發現{number}元！{page} | `fdps_battle_search_cell_at_cursor`（`src/btlact.c`） |
| `0x213` | 您的錢不夠了！{page} | `fdps_shop_buy_loop`（`src/shop.c`） |

### `0x214`–`0x21c`：教會轉職

| 條目 | 原文 | 讀取端 |
| --- | --- | --- |
| `0x214` | 沒有人可以轉職！{page} | `fdps_church_promote_loop`（`src/church.c`） |
| `0x215` | {subst1}要轉成{subst2}的職業嗎？ | `fdps_church_promote_loop`（`src/church.c`） |
| `0x216` | {subst1}轉成{subst2}的職業！ | `fdps_church_promote_loop`（`src/church.c`） |
| `0x217` | 移動力（MV）增加{number}點！{page} | `fdps_church_promote_loop`（`src/church.c`） |
| `0x218` | 攻擊力（AP）增加{number}點！ | `fdps_church_promote_loop`（`src/church.c`） |
| `0x219` | 防禦力（DP）增加{number}點！ | `fdps_church_promote_loop`（`src/church.c`） |
| `0x21a` | 速度（DX）增加{number}點！{page} | `fdps_church_promote_loop`（`src/church.c`） |
| `0x21b` | MHP增加{number}點！ | `fdps_church_promote_loop`（`src/church.c`） |
| `0x21c` | MMP增加{number}點！{page} | `fdps_church_promote_loop`（`src/church.c`） |

### `0x21d`–`0x221`：道具使用：強化套件與高能量裝置

| 條目 | 原文 | 讀取端 |
| --- | --- | --- |
| `0x21d` | 這是什麼？{page} | `fdps_apply_item_effect_to_targets`（`src/item.c`） |
| `0x21e` | ··＃····％··{br}·＊··＆···＠·{br}強化套件安裝完畢！{page}MV　＋　1{br}AP　＋　30{br}DP　＋　30{page}轟神砲禁制解除！{page} | `fdps_apply_item_effect_to_targets`（`src/item.c`） |
| `0x21f` | ··＃····％··{br}·＊··＆···＠·{br}強化套件安裝完畢！{page}HP　＋　100 | `fdps_apply_item_effect_to_targets`（`src/item.c`） |
| `0x220` | 好棒的能量發生裝置！{page}要是有外殼材料，{br}就可以做出一把很棒的{br}武器了···{page} | `fdps_apply_item_effect_to_targets`（`src/item.c`） |
| `0x221` | 好棒的能量發生裝置！{br}正好有金屬礦···{page}好，{br}就讓我大顯身手一番！{page}··＃····％··{br}·＊··＆···＠·{br}完成了！高能量砲！{page} | `fdps_apply_item_effect_to_targets`（`src/item.c`） |

### `0x222`–`0x223`：換片提示

| 條目 | 原文 | 讀取端 |
| --- | --- | --- |
| `0x222` | 請放入Disk　1 | `fdps_cd_verify_disc_and_play_track`（`src/cdaudio.c`） |
| `0x223` | 請放入Disk　2 | `fdps_cd_verify_disc_and_play_track`（`src/cdaudio.c`） |

### `0x224`–`0x229`：酒店抽獎

| 條目 | 原文 | 讀取端 |
| --- | --- | --- |
| `0x224` | 新年快樂！{br}這位客人您可以參加我們的{br}新年摸彩活動喔！{page}那麼我們開始吧！{page} | `fdps_run_bonus_lottery`（`src/vilbar.c`） |
| `0x225` | 哇～～～恭～喜～您～～{br}得到本次摸彩的特獎～～{br}斬～鐵～劍～～一把！{page} | `fdps_run_bonus_lottery`（`src/vilbar.c`） |
| `0x226` | 恭～喜～您～～{br}得到二獎水晶粒10顆！{page} | `fdps_run_bonus_lottery`（`src/vilbar.c`） |
| `0x227` | 三～獎～～～～～～～{br}賞金20000元～～～{page} | `fdps_run_bonus_lottery`（`src/vilbar.c`） |
| `0x228` | 噗～～～～～～～～～{br}很～遺～憾～～～{br}您得到參加獎藥草一把！{page} | `fdps_run_bonus_lottery`（`src/vilbar.c`） |
| `0x229` | 那麼請繼續冒險吧！{page} | `fdps_run_bonus_lottery`（`src/vilbar.c`） |

### `0x22a`：換片之後

| 條目 | 原文 | 讀取端 |
| --- | --- | --- |
| `0x22a` | 請稍待片刻！ | `fdps_cd_verify_disc_and_play_track`（`src/cdaudio.c`） |
