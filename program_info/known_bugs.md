# 已知原版 bug

**驗證對象**：`FDPS.LE` 原版執行檔裡玩家玩得出來的程式缺陷，每一條的成因以此檔為唯一正典。涵蓋的 function 與所在的 `src/` 檔：`0x19f80`（全螢幕物理結算，`src/combat.c`）；`0x1c3a0`、`0x1c520`（地圖上的物理結算，`src/unitatk.c`）；`0x1dd30`、`0x28460`、`0x29080`（經驗、傷害與異常免疫，`src/unitstat.c`）；`0x18b10`（`ENEMYDAT.DAT` 取記錄，`src/table.c`）；`0x126b0`、`0x12c10`（`src/mapai.c`）、`0x12230`、`0x13040`、`0x132b0`（`src/aiscore.c`）、`0x12e50`、`0x27180`（`src/aiact.c`）、`0x11460`（`src/movegrid.c`）、`0x11e50`（`src/aitarget.c`）、`0x2b4f0`、`0x2c6a0`、`0x2d7c0`（`src/mapcur.c`）（地圖 AI、目標收集與游標）；`0x252b0`、`0x262a0`（玩家使用道具，`src/item.c`）；`0x3a2e0`（`src/btlend.c`）、`0x3aad0`（`src/chpost1.c`）、`0x3ae80`、`0x3b150`、`0x3b3d0`（`src/chpost2.c`）、`0x39e70`（`src/roster.c`）（章節勝敗判定與章節結束）；`0x21650`（`src/icon.c`）、`0x2db50`（`src/unit.c`）、`0x374e0`（`src/chevt2.c`）、`0x12960`、`0x15470`（`src/btlturn.c`）、`0x14ab0`（`src/btlmenu.c`）（戰鬥中播放的過場）；`0x2a2b0`（`src/title.c`）、`0x241e0`（`src/save.c`）（標題選單與存檔）；`0x31210`、`0x357a0`（`src/village.c`）、`0x36460`（`src/vilbar.c`）、`0x36af0`（`src/roster.c`）（村莊）；`0x39550`（`src/chevt6.c`）（第 28 章援軍）；`0x18d60`（`src/combat.c`）、`0x1a4c0`（`src/cmbspell.c`）、`0x2a140`（`src/vfs.c`）（戰鬥畫面素材的載入）；`0x1d990`（`src/death.c`）、`0x25d20`（`src/unititem.c`）（死亡腳本的掉落物）。

依功能等價（[ADR-0001](../docs/adr/0001-only-functional-equivalence.md)），下列每一條重建版都照原樣保留。每條寫三件事：**現象**是玩家看到什麼，**成因**是程式哪裡做錯，**重建**一行指向重建時會怎麼寫錯的正典（[`../rebuild_info/pitfalls.md`](../rebuild_info/pitfalls.md) 的對應列，或該 function 的 plate comment `Rebuild note`）。各機制頁只以一行提到這些 bug 並連回這裡；被 bug 封住的內容由 [`cut_content/`](../cut_content/_index.md) 擁有，這裡只寫一行。

原版之所以會讀到「陣列之外」的值（第 4、12、13、22 條）或「堆疊上的殘值」（第 18、19 條），而且每次都讀到同一種結果，是因為堆積與堆疊的佈局只由呼叫序列決定；佈局本身由 [`memory_layout.md`](memory_layout.md) 擁有。

## 清單

| # | 名稱 | 所在 | 後果 |
| ---: | --- | --- | --- |
| 1 | 連擊的經驗只算最後一擊 | `fdps_combat_compute_hit_outcome`（`0x19f80`） | 沒打死時第一擊的經驗被蓋掉 |
| 2 | 蓋亞升上 40 級時經驗餘數歸零 | `fdps_unit_award_exp_and_level_up`（`0x1dd30`） | 一次性少最多 98 點經驗 |
| 3 | 關掉戰鬥動畫時武器異常幾乎無人免疫 | `fdps_unit_resolve_attack_hit`（`0x1c520`） | 免疫的敵人照樣中毒、麻痺 |
| 4 | 陣營 0、肖像編號小於 `0x3c` 的單位給的經驗讀自表外 | `fdps_get_enemy_record`（`0x18b10`） | 經驗值由記憶體殘值決定 |
| 5 | 神聖之水補錯邊 | `fdps_map_actor_use_item`（`0x27180`） | 敵人補我方、NPC 補敵人 |
| 6 | 三種行動分數相同時 AI 原地不動 | `fdps_map_actor_take_best_action`（`0x12c10`） | 該單位整回合不動也不休息 |
| 7 | 「走得到的最近對手」用錯職業的地形消耗 | `fdps_map_actor_move_toward_nearest_reachable_opponent`（`0x126b0`） | 敵人選錯要走向的目標 |
| 8 | 友軍 NPC 的「走得到的最近對手」找到自己 | `fdps_move_path_trace`（`0x11460`） | NPC 不沿路找敵人 |
| 9 | 敵人的光束道具軌跡畫出地圖 | `fdps_map_actor_use_item`（`0x27180`） | 軌跡出現在錯誤的格子上 |
| 10 | 大地之劍、白銀之槍在玩家手上「使用」無效 | `fdps_battle_item_menu`（`0x252b0`） | 一件打空、一件無法確認；封住白銀之槍的使用效果 |
| 11 | 第 14、20 章的法蓮娜死亡不算敗北 | `fdps_battle_check_default_end_conditions`（`0x3a2e0`） | 與畫面上的失敗條件不符 |
| 12 | 第 19 章提早清場後接受決鬥，戰鬥結束不了 | `fdps_chapter_19_post_action`（`0x3ae80`） | 卡關 |
| 13 | 第 24 章提早清場後接受決鬥，挑戰者自動認輸 | `fdps_chapter_24_post_action`（`0x3b3d0`） | 不戰而勝 |
| 14 | 陣亡 7 人以上時復活面板是空的 | `fdps_roster_revive_fallen_members`（`0x39e70`） | 看不到復活名單與費用 |
| 15 | 第 8 章牢門過場讓已行動的單位重新可動 | `fdps_icon_script_run`（`0x21650`） | 一回合多動一次 |
| 16 | 只存過 slot 的玩家從標題進不了讀檔畫面 | `fdps_title_screen`（`0x2a2b0`） | 「讀檔」反灰 |
| 17 | 標題讀檔畫面取消後，可能跳過一次存檔畫面 | `fdps_run_village_phase`（`0x31210`） | 章節之間沒機會存檔 |
| 18 | 酒館抽獎永遠只發藥草 | `fdps_run_bonus_lottery`（`0x36460`） | 封住三種大獎 |
| 19 | 第 26 章前的神秘商店進不去 | `fdps_check_secret_code_key`（`0x357a0`） | 封住那一家的貨 |
| 20 | 第 28 章援軍的波次 3 出場兩次、波次 4 永不出場 | `fdps_chapter_28_event_deploy_wave_for_turn`（`0x39550`） | 封住三名敵兵 |
| 21 | 第 17、23 章幾個沒有戰鬥畫面的單位被捲入全螢幕戰鬥時程式結束 | `fdps_vfs_load_entry`（`0x2a140`） | 回到 DOS（潛在，觸發未經實機確認） |
| 22 | 第 10 章一名敵兵掉出表外物品 `FF` | `fdps_run_death_scripts`（`0x1d990`） | 背包多一件名為「裂地術」、數值不定的物品 |

## 1. 連擊的經驗只算最後一擊

**現象**：我方的物理攻擊觸發連擊、第一擊沒打死敵人時，得到的經驗只按第二擊算：第二擊命中，給的是「只打第二擊」那麼多；第二擊落空，完全不給經驗，也不出經驗視窗。第一擊造成的傷害沒有換到任何經驗。一擊打死目標則照常給全額。

**成因**：全螢幕的 `fdps_combat_play_blow`（`0x196d0`）每一擊呼叫一次 `fdps_combat_compute_hit_outcome`（`0x19f80`），它在攻方陣營 2、守方陣營 0 時把這一擊的經驗**指派**給經驗累加器 `data_fdps_battle_pending_xp_credit`（`fdps_combat_compute_hit_outcome` 內的 `0x1a330` 與 `0x1a34d`，`MOV [0x69cec],EAX`），而不是加上去；地圖上的 `fdps_unit_resolve_attack_hit`（`0x1c520`）同樣是指派（`fdps_unit_resolve_attack_hit` 內的 `0x1ca68` 與 `0x1ca85`）。法術傷害 `fdps_unit_apply_damage`（`0x28460`）等其他結算則是累加。第二擊的結算因此把第一擊算出的值整個蓋掉；落空的一擊傷害是 0，按「傷害 ÷ HP 上限」比例算出的經驗也是 0，而 `fdps_unit_award_exp_and_level_up`（`0x1dd30`）在累加器為 0 時直接返回。經驗公式本身見 [`battle.md`](battle.md)。

**重建**：照原樣保留，兩支結算都寫指派。

## 2. 蓋亞升上 40 級時經驗餘數歸零

**現象**：蓋亞在某次結算中從 39 級升上 40 級時，升級後剩下的經驗（最多 98 點）被丟掉，經驗從 0 重新累積；之後照常一路升到 99 級。

**成因**：`fdps_unit_award_exp_and_level_up`（`0x1dd30`）入口的等級上限檢查分兩支：肖像編號 9（蓋亞）比 99，其他人比 40。升級後的餘數檢查（`fdps_unit_award_exp_and_level_up` 內的 `0x1e2f4`）寫成「肖像編號 9 且新等級 99 → 餘數 0；**否則**新等級 40 → 餘數 0」，第二條沒有排除肖像編號 9，於是蓋亞在不是上限的 40 級也被歸零。累加器先被夾在 99（`fdps_unit_award_exp_and_level_up` 內的 `0x1ddb1`），加上小於 100 的舊餘數，一次結算最多升一級，扣掉 100 後的餘數最多 98。

**重建**：照原樣保留，餘數檢查是 if／else-if 兩條，第二條不看肖像編號。

## 3. 關掉戰鬥動畫時，武器附加的異常幾乎無人免疫

**現象**：戰鬥動畫關閉時，敵人或友軍 NPC 發動的交戰改在地圖上結算。這時帶中毒、麻痺效果的武器（包括我方單位在那場交戰裡的反擊）能讓職業是守護獸、將軍、惡靈、活屍等原本免疫的單位，以及肖像編號 `0x3c`–`0x44` 的敵人中毒或麻痺；動畫開著時同一把武器對同一個目標不會生效。

**成因**：`fdps_map_actor_move_and_attack`（`0x12e50`）在動畫開關為 0 時改呼叫 `fdps_unit_attack_target`（`0x1c3a0`），每一擊由 `fdps_unit_resolve_attack_hit`（`0x1c520`）結算，它的免疫判定只看守方職業是否 `0x19`（`fdps_unit_resolve_attack_hit` 內的 `0x1c70b` 與 `0x1c7f7`）。全螢幕的 `fdps_combat_compute_hit_outcome`（`0x19f80`）改問 `fdps_unit_is_ailment_immune`（`0x29080`），它另外涵蓋職業 `0x21`–`0x22`、`0x24`–`0x26` 與肖像編號 `0x3c`–`0x44`。我方主動攻擊一律走全螢幕，不受動畫開關影響。兩條結算路徑的其他差異見 [`battle.md`](battle.md)，職業代碼見 [`../assets/tables/classes.md`](../assets/tables/classes.md)。

**重建**：照原樣保留，兩條結算路徑不合併，地圖上那條只比職業 `0x19`。

## 4. 陣營 0、肖像編號小於 `0x3c` 的單位給的經驗讀自表外

**現象**：第 13 章（`MAP12`）有一個陣營 0、肖像編號 `0D` 的敵人，第 15、19、24 章（`MAP14`、`MAP18`、`MAP23`）決鬥波次的挑戰者是陣營 0、肖像編號 `23`。我方用全螢幕攻擊、法術或傷害道具打這幾個單位時，得到的經驗不是任何資料表裡的數，而是記憶體裡剛好在那個位置的值（最後仍被 99 夾住）。

**成因**：`fdps_combat_compute_hit_outcome`（`0x19f80`）與 `fdps_unit_apply_damage`（`0x28460`）只以守方陣營 byte 為 0 判定「這是敵人」，就把「肖像編號 − `0x3c`」交給 `fdps_get_enemy_record`（`0x18b10`）；後者直接回傳 `ENEMYDAT.DAT` 緩衝區 + 索引 × 10，不檢查範圍。肖像 `0D` 得索引 −47、`23` 得 −25，經驗欄位（記錄 `+9`）分別讀自緩衝區之前第 461 與 241 byte 的堆積內容。地圖上的 `fdps_unit_resolve_attack_hit`（`0x1c520`）另外要求肖像編號 ≥ `0x3c`，對這些單位不給經驗。部署記錄的陣營與角色編號是兩個獨立欄位，格式見 [`../resource_info/map.md`](../resource_info/map.md)，各章的部署見 [`../chapters/_index.md`](../chapters/_index.md)。

**重建**：照原樣保留，不補下界檢查，見 [`pitfalls.md` 的「不能加的檢查」](../rebuild_info/pitfalls.md#不能加的檢查)。

## 5. 神聖之水補錯邊：敵人補我方，友軍 NPC 補敵人

**現象**：第 24 章第 4 波的 26 名敵人帶著神聖之水。一名 HP ≤ 上限 1/3 的我方或友軍單位站在這種敵人的上下左右時，只要敵人沒有打得死人的攻擊、也沒有更高分的法術，它就對那名單位使用神聖之水（名目回復量 1000），把對方補回來。第 26 章第 3 波的 4 名友軍 NPC 也帶著神聖之水，會拿來補站在旁邊的重傷敵人。

**成因**：AI 的道具評分 `fdps_map_actor_score_best_item`（`0x13040`）與執行 `fdps_map_actor_use_item`（`0x27180`）在敵方階段（side_select 0）把 `ITEM.DAT` 的對象 byte（`+0x11`）做布林反轉（`byte == 0`），友軍階段直接用原值，結果當 `fdps_collect_targets_in_range`（`0x11e50`）的陣營篩選（0 收陣營 0、1 收陣營 ≠ 0、2 收陣營 1、3 收陣營 2，其他值一個都不收）。藥草、回復劑、再生藥（`B4`–`B6`）的對象 byte 是 5，反轉成 0，敵人補的是自己人；神聖之水（`C2`）的對象 byte 是 0，反轉成 1，敵人補的是我方與友軍；友軍階段的 0 則是陣營 0，也就是敵人。評分 `fdps_score_targets_for_item`（`0x132b0`）給 HP ≤ 上限/3 的目標 8 分、≤ 上限/2 的 3 分，不看道具的回復量；攻擊評分 `fdps_map_actor_score_best_attack`（`0x12230`）的非致命一擊也是 8 分，而 `fdps_map_actor_take_best_action`（`0x12c10`）在攻擊與道具同分、法術較低時選道具。同一個反轉讓 `B4`–`B6` 在友軍階段變成模式 5，友軍 NPC 永遠不會使用這三種藥。道具數值見 [`../assets/items.md`](../assets/items.md)，AI 評分的完整規則見 [`map_ai.md`](map_ai.md)。

**重建**：照原樣保留，不改 `ITEM.DAT` 也不在 AI 端特判神聖之水。

## 6. 三種行動分數完全相同時，AI 原地不動

**現象**：敵人或友軍 NPC 的攻擊、法術、道具三個最佳分數完全相等，而且至少達 6（例如都是 8）時，它這回合什麼都不做：不攻擊、不施法、不用道具，也不移動、不休息。

**成因**：`fdps_map_actor_take_best_action`（`0x12c10`）在任一分數 ≥ 6 之後依序測五個條件，A、S、I 分別是攻擊、法術、道具分數：A > S 且 A > I；A = S 且 S > I；A = I 且 I > S；S > A 且 I ≤ S；I > A 且 I > S。三者相等時五條都不成立，沒有任何行動函式被呼叫，函式卻照樣把游標模式清 0 並回傳 1；呼叫它的 `fdps_map_actor_behavior_step`（`0x10010`）把 1 當成「已行動」，不再走移動或休息的後備步驟。

**重建**：照原樣保留，是五個明寫的比較而不是取最大值，見該 function 的 plate comment `Rebuild note`。

## 7. 「走得到的最近對手」用錯職業的地形消耗

**現象**：敵人挑選要走向哪個對手時，用來比較「誰走得最近」的地形消耗屬於另一個職業。某種地形擋得住它、卻擋不住那個職業時，它會朝一個自己其實繞不過去的目標走；反過來也可能捨近求遠。

**成因**：`PROMAP.DAT` 第 0 列是全 1 的預設列，其他呼叫端一律以「職業代碼 + 1」取職業的地形消耗（例如 `fdps_map_actor_score_best_attack`（`0x12230`））。`fdps_map_actor_move_toward_nearest_reachable_opponent`（`0x126b0`）把職業代碼直接交給 `fdps_get_class_record`（`0x18b70`），漏了 +1，再以 100 點預算讓 `fdps_move_grid_flood_fill_range`（`0x10de0`）擴散整張地圖，所以用的是職業代碼少 1 那個職業的消耗；職業 0 則拿到全 1 的預設列，等於不看地形。表的內容見 [`../assets/tables/classes.md`](../assets/tables/classes.md)。

**重建**：照原樣保留，見 [`pitfalls.md` 的「不能修的原版 bug」](../rebuild_info/pitfalls.md#不能修的原版-bug)。

## 8. 友軍 NPC 的「走得到的最近對手」找到的是自己

**現象**：友軍 NPC 不會沿著走得通的路去找最近的敵人：有「朝直線距離最近的對手走」這個後備步驟的行為，改朝直線最近的敵人走，常被地形卡住；沒有這一步的行為就原地休息。

**成因**：第 7 條那支 `fdps_map_actor_move_toward_nearest_reachable_opponent`（`0x126b0`）以模式 2 呼叫 `fdps_move_path_trace`（`0x11460`）找消耗最低的單位，模式 2 的陣營篩選參數寫死成 0（收陣營 ≠ 0 的單位），不是依 side_select 換邊。敵方階段這剛好正確；友軍階段的行動者自己就是陣營 1，站在消耗 0 的起點上贏得搜尋，函式發現「目標格就是自己的格」而回傳 0，由行為分派改走後備步驟。各行為值的後備步驟見 [`map_ai.md`](map_ai.md)。

**重建**：照原樣保留，模式 2 的第四個參數是字面值 0。

## 9. 敵人的光束道具軌跡畫出地圖

**現象**：第 15 章持光束砲、第 29 章持火焰的敵人朝地圖右側或下側發射時，發射後停留 8 幀的軌跡高亮會延伸出地圖：超出右緣的那一段出現在下一列左側的格子上。

**成因**：`fdps_map_actor_use_item`（`0x27180`）把直線道具的軌跡終點外推為「施放者格 +（`use_distance` − `0x10`）×（瞄準格 − 施放者格）」，再以第 0 圖層 `.MPL` 區塊開頭的兩個 16 位元字夾限。那兩個字是檔案 magic `"MPL\0"`，讀出 x 界限 `0x504d`、y 界限 `0x004c`（76），不是地圖寬高（`.MPL` 的格式見 [`../resource_info/terrain.md`](../resource_info/terrain.md)），所以終點可以落在地圖外。游標以模式 6 掃向終點，`fdps_map_cursor_move_to`（`0x2d7c0`）不夾游標座標，`fdps_draw_map_cursor`（`0x2c6a0`）在每一步把移動網格第 (y ÷ 24) × 寬 + x ÷ 24 格的標記 byte 寫成 0，也不檢查範圍：x 超過寬度就落到下一列，y 超過最後一列就寫到網格區塊之後的堆積上。

**重建**：照原樣保留，夾限照讀 `.MPL` 開頭的兩個字，見該 function 的 plate comment `Rebuild note`。

## 10. 大地之劍、白銀之槍在玩家手上「使用」無效

**現象**：對大地之劍按「使用」：選好位置後畫面震動、播地震音效，沒有任何人受傷，該單位的回合結束。對白銀之槍按「使用」：瞄準游標出現，但確認鍵沒有反應，只能取消。敵人持有時兩者都正常生效。

**成因**：玩家的道具使用 `fdps_battle_item_menu`（`0x252b0`）拿物品記錄的 `select_mode`（`+0x15`）同時當 `fdps_collect_targets_in_range`（`0x11e50`）的陣營篩選與 `fdps_map_cursor_select_loop`（`0x2b4f0`）的確認模式。篩選只認 0–3，其他值一個單位都不收。大地之劍（`0C`）的 `select_mode` 是 4：確認迴圈的模式 4 接受任何標出的格，確認後以模式 4 收集命中名單，永遠是空的；`fdps_apply_item_effect_to_targets`（`0x262a0`）的地系分支照樣播 `EarQu.wav`、震動畫面、對 0 個目標結算，接著 `fdps_battle_mark_unit_done`（`0x119b0`）結束回合。白銀之槍（`22`）的 `select_mode` 是 5：確認迴圈的模式 5 對確認鍵不做任何事。AI 使用道具時改看 `use_target`（`+0x11`），這兩件都是 0，所以敵人手上正常。道具數值見 [`../assets/items.md`](../assets/items.md)。

大地之劍玩家本來就拿不到（沒有任何取得途徑，也沒有單位帶著），所以實際玩得出來的只有白銀之槍那一半。

**被封住的內容**：白銀之槍的冰系傷害使用效果，見 [`cut_content/items.md`](../cut_content/items.md) 的 I10。

**重建**：照原樣保留，玩家端的篩選與確認模式都取 `select_mode`，見 [`pitfalls.md` 的「不能修的原版 bug」](../rebuild_info/pitfalls.md#不能修的原版-bug) 的玩家端道具選取模式一列。

## 11. 第 14、20 章的法蓮娜死亡不算敗北

**現象**：這兩章的勝敗條件視窗寫著失敗條件「蘭迪斯死亡／法蓮娜死亡」，但法蓮娜陣亡時戰鬥照常進行，不會敗北。

**成因**：視窗顯示的是 `FDETXT14`、`FDETXT20` 的第 3 筆，只是文字，不參與判定。判定由 `fdps_chapter_14_post_action`（`0x3aad0`）直接轉呼、`fdps_chapter_20_post_action`（`0x3b150`）先依回合改寫 AI 行為再轉呼 `fdps_battle_check_default_end_conditions`（`0x3a2e0`）；共用判定只在章節索引 `0x10`、`0x15`（第 17、22 章）看單位 3，其他章一律只看單位 0（蘭迪斯）是否退場，沒有任何程式檢查這兩章的法蓮娜。各章的勝敗規則見 [`chapter.md`](chapter.md)。

**重建**：照原樣保留，不照文字補判定，見 [`pitfalls.md` 的「不能照字面理解的資料」](../rebuild_info/pitfalls.md#不能照字面理解的資料)。

## 12. 第 19 章提早清場後接受決鬥，戰鬥結束不了

**現象**：在該章援軍全部出場之前清完敵人，而裘娜帶著妖刀村雨時，會出現狂戰士的決鬥邀請；接受之後對手消失，地圖上只剩裘娜，戰鬥再也不會結束。攻略站記載的也是「狂戰士會消失，造成本章無法結束」。

**成因**：`fdps_chapter_19_post_action`（`0x3ae80`）在共用判定寫出過關（2）、回合 ≤ 20、決鬥旗標未設、裘娜（單位 4）未退場且帶著 `A5` 時，以 `fdps_deploy_wave`（`0x23830`）部署挑戰者並詢問玩家。接受時它把單位 0 到 `0x4c`（字面值）除單位 4 以外全部設為退場，把結束碼改回 0；旗標設起之後它不再呼叫共用判定，只以 `fdps_unit_is_retired`（`0x109b0`）問單位 4 或單位 `0x4d` 有沒有退場。挑戰者附加在單位陣列尾端，只有援軍都已出場、陣列正好有 `0x4d` 筆時它才落在 `0x4d`。提早清場時它落在較小的索引，被退場掃描一起設為退場；判定問的 `0x4d` 已在陣列之外，讀到的是單位陣列之後的堆積內容，結束碼停在 0。

**重建**：照原樣保留，判定與掃描都用寫死的索引；陣列之外讀到什麼由堆積佈局決定，見 [`pitfalls.md` 的「不能修的原版 bug」](../rebuild_info/pitfalls.md#不能修的原版-bug)。

## 13. 第 24 章提早清場後接受決鬥，挑戰者自動認輸

**現象**：在第 7 回合之前清完敵人，接受狂戰士的決鬥後，第一回合我方結束時狂戰士就被判落敗，裘娜不戰而勝、妖刀村正換成妖刀正宗。攻略站記載相同。

**成因**：`fdps_chapter_24_post_action`（`0x3b3d0`）的結構與第 12 條相同（回合 ≤ 25、裘娜帶著 `A6`），勝負判定問的是寫死的單位 `0x52`。它的退場掃描上界是「目前單位數 − 1」，剛附加在尾端的挑戰者倖存；但只有五個回合波次都已出場時挑戰者才落在 `0x52`。提早清場時 `0x52` 在陣列之外，`fdps_unit_is_retired`（`0x109b0`）讀到的堆積內容表現為「已退場」，於是走裘娜獲勝的分支：收走 `A6`、給 `A7`、結束碼寫 2。

**重建**：照原樣保留，判定用寫死的 `0x52`；堆積佈局一變，挑戰者會不會認輸跟著變，見 [`pitfalls.md` 的「不能修的原版 bug」](../rebuild_info/pitfalls.md#不能修的原版-bug)。

## 14. 陣亡 7 人以上時，復活面板是空的

**現象**：章節結束時若有 7 名以上隊員陣亡，復活費用面板只剩一個空框，看不到誰被復活、各花多少錢；錢照樣扣，面板照樣等按鍵。

**成因**：`fdps_roster_revive_fallen_members`（`0x39e70`）逐列繪製時，每一列都包在「復活人數 < 7」的條件裡，比的是總人數而不是列號，所以總人數達 7 就一列都不畫。復活與扣款在畫面之前就做完，不受影響；錢不夠時先扣成負數，面板關閉後才夾回 0。費率表與章節結束的流程見 [`chapter.md`](chapter.md)。

**重建**：照原樣保留，條件比的是總人數，見該 function 的 plate comment `Rebuild note`。

## 15. 第 8 章牢門過場讓已行動的單位重新可動

**現象**：第 8 章若由我方單位先於客將費塔加走上牢門格，過場播完後，本回合已經行動過（變暗）的我方單位全部恢復成可行動，連開門的那個也能再動一次。用「全軍前進」途中開門時，索引排在開門者之後、本回合在全軍前進之前就已行動過的我方單位會被再移動一次。敵人在敵方階段走上牢門格時，部分敵人一個階段能行動兩次。

**成因**：過場直譯器 `fdps_icon_script_run`（`0x21650`）一進入就呼叫 `fdps_units_clear_status_bit7`（`0x2db50`），把地圖上所有單位狀態 byte 的 bit 7（本回合已行動）清掉。戰鬥中會播過場的只有第 8 章，其中牢門格（單位走完一步觸發）的處理函式 `fdps_chapter_08_event_villagers_leave_cells`（`0x374e0`）不看是誰踩上，一章一次播放 `ICON7-3`。各階段迴圈都以 bit 7 判定誰還能動：`fdps_battle_unit_turn`（`0x15470`）先以 `fdps_battle_mark_unit_done`（`0x119b0`）設 bit 7、再派送格子事件，開門者自己也被清掉；`fdps_battle_system_menu`（`0x14ab0`）的全軍前進依索引逐一以 `& 0x85` 挑單位、先派送事件後標已行動，排在觸發者之後、原本因 bit 7 被略過的單位因此被挑中；`fdps_battle_enemy_turn_phase`（`0x12960`）兩遍掃描都以 `& 0x81` 判定可行動，觸發時已行動過的敵人會在後面的掃描裡再動一次。第 8 章另外兩支過場在敵方回合一開始播放，當時沒有單位行動過，清除沒有可見的效果。各階段的完整情形見 [`cutscene.md`](cutscene.md)。

**重建**：照原樣保留，清除在直譯器入口做，見該 function 的 plate comment `Rebuild note`。

## 16. 只存過 slot 的玩家，從標題進不了讀檔畫面

**現象**：只在章節之間的存檔畫面存過檔、從沒在戰鬥中存過檔的玩家，重開遊戲後標題選單的「讀檔」是灰的，只能開新遊戲；那些 slot 要等走到有村莊的章節，從酒館的「讀檔」才讀得到。

**成因**：`fdps_title_screen`（`0x2a2b0`）進入時讀 `FDE.SAV`，把「讀檔」（進 slot 讀檔畫面）與「繼續」（讀戰鬥存檔）綁在同一個條件上：檢查碼相符，**而且**戰鬥存檔區的章節 byte（`+0x30c5`）不是 `0xff`，兩項才一起變成可選。檔案不存在時，存檔畫面 `fdps_save_game_screen`（`0x241e0`）把整份映像填成 `0xff` 再寫入 slot，從不動戰鬥存檔區；那一區由戰鬥中系統選單的存檔 `fdps_battle_system_submenu`（`0x14ea0`）寫入。存檔的佈局見 [`../resource_info/save.md`](../resource_info/save.md)，存讀檔的流程見 [`save.md`](save.md)。

**重建**：照原樣保留，見 [`pitfalls.md` 的「不能修的原版 bug」](../rebuild_info/pitfalls.md#不能修的原版-bug)。

## 17. 標題讀檔畫面取消後，可能跳過一次章節之間的存檔畫面

**現象**：在標題選單打開讀檔畫面後按 Esc 取消，改選「繼續」接回戰鬥；打完那一章後，如果下一章是沒有村莊的章節，章節之間的存檔畫面不會出現，直接開始下一章。

**成因**：`fdps_title_screen`（`0x2a2b0`）的讀檔分支在 `fdps_load_game_screen`（`0x24490`）回來後，不論結果都把 `data_fdps_village_skip_save_prompt_flag` 設成 1（`fdps_title_screen` 內的 `0x2a8c8`）。這個旗標的用意是「剛從 slot 讀檔進入無村莊章節時不必再問一次存檔」；讀檔被取消時標題選單重新出現，旗標卻留著 1。全映像只有 `fdps_run_village_phase`（`0x31210`）的結尾（`fdps_run_village_phase` 內的 `0x31513`）會把它清回 0。之後的村莊階段若遇上無村莊章節（章節索引 `0x10`、`0x11`、`0x15`、`0x16` 或大於 `0x19`），`fdps_run_village_phase` 在 `0x3127c` 看到旗標為 1，就略過本來要開的 `fdps_save_game_screen`（`0x241e0`）。改選「新遊戲」時，第一個村莊階段（第 2 章前）就會清掉旗標，不受影響。

**重建**：照原樣保留，旗標在讀檔分支無條件設 1。

## 18. 酒館抽獎永遠只發藥草

**現象**：電腦日期為 1998 年 1 月 28 日時進酒館會觸發一次抽獎。轉盤與公告照實顯示停在斬鐵劍、水晶粒或兩萬金，但實際發下的一律是藥草：背包未滿 8 格的隊員各一個（章節索引 `0x17` 以後另跳過名冊第 3 格）。

**成因**：`fdps_run_bonus_lottery`（`0x36460`）在 `0x3691a` 以堆疊槽 `[EBP-0xc]` 選擇發哪一種獎（0 斬鐵劍、1 十個水晶粒、2 兩萬金、其他藥草），而這個槽唯一的寫入在 `fdps_run_bonus_lottery` 內的 `0x36a05`，位在發獎之後；寫入的值是轉盤停下那一格的獎項類別，只拿去組公告。發獎時讀到的是前一個呼叫留在那一格的殘值，原版恆不等於 0、1、2，於是一律走到藥草分支，由 `fdps_roster_add_item_to_all`（`0x36af0`）發 `B4`；它依名冊順序只給物品數不是 8 的成員，章節索引 `0x17` 以後跳過名冊第 3 格。抽獎的流程見 [`village.md`](village.md)。

**被封住的內容**：抽獎的三種大獎，見 [`cut_content/items.md`](../cut_content/items.md) 的 I06。

**重建**：照原樣保留，重建版讀到的殘值也要落在 0、1、2 之外，見 [`pitfalls.md` 的「不能修的原版 bug」](../rebuild_info/pitfalls.md#不能修的原版-bug)。

## 19. 第 26 章前的神秘商店進不去

**現象**：第 25 章打完、進第 26 章之前的村莊，看板選單的神秘商店無論輸入什麼暗號都進不去。攻略站也記載「無法進入，只能透過修改」。

**成因**：`fdps_run_village_phase`（`0x31210`）讓章節索引 1–15、18–20、23–25 都有村莊，但看板選單 `fdps_village_signboard_menu`（`0x31bc0`）呼叫的 `fdps_check_secret_code_key`（`0x357a0`）只有 24 列、每列 8 byte 的暗號表：它把表複製到自己的堆疊框架 `[EBP-0xc4]`，以 `[EBP-0xcc + 章節索引 × 8 + 目前位置]` 取字元，也就是第「章節索引 − 1」列。章節索引 25 取到第 24 列，落在表外：前 4 byte 是這支函式自己還沒寫入的回傳值槽 `[EBP-4]`，後 4 byte 是存起來的呼叫端 EBP。暗號因此變成執行期堆疊上的殘值與位址 byte，不是按鍵掃描碼湊得出來的序列。暗號比對的規則見 [`village.md`](village.md)。

**被封住的內容**：這座神秘商店本身見 [`cut_content/code.md`](../cut_content/code.md) 的 C1，`SHOP25.DAT` 神秘商店那一列的貨見 [`cut_content/items.md`](../cut_content/items.md) 的 I09。

**重建**：照原樣保留，表長 24 列、以「章節索引 − 1」取列，不補範圍檢查，見 [`pitfalls.md` 的「不能修的原版 bug」](../rebuild_info/pitfalls.md#不能修的原版-bug) 的村莊暗號表一列。

## 20. 第 28 章援軍的波次 3 出場兩次、波次 4 永不出場

**現象**：第 28 章第 6 回合與第 7 回合各來一批同樣的援軍（地獄犬、骷顱兵、幽魂各一，LV25，從上緣出現）；地圖資料裡準備好的波次 4（幽魂 1 名、地獄犬 2 名）整章都不會出現。

**成因**：`MAP27.DAT` 的回合事件在第 2、4、6、7、10、12、14、16、18 回合呼叫腳本事件 slot 44 的 `fdps_chapter_28_event_deploy_wave_for_turn`（`0x39550`），它以「回合 ÷ 2」的整數商當波次交給 `fdps_deploy_wave`（`0x23830`）。除法向零截斷，第 6、7 回合都得 3；`fdps_deploy_wave` 不記得哪一波出場過，所以波次 3 再部署一次，而九個排定回合沒有一個算得出 4。過場腳本與其他處理函式也不部署這張地圖的波次 4。算式與分派見 [`chapter.md`](chapter.md)，部署記錄的格式見 [`../resource_info/map.md`](../resource_info/map.md)。

**被封住的內容**：波次 4 的三名敵兵，見 [`cut_content/story.md`](../cut_content/story.md) 的 S10。

**重建**：照原樣保留，算式照寫「回合 ÷ 2」、排程照地圖資料，見 [`pitfalls.md` 的「不能修的原版 bug」](../rebuild_info/pitfalls.md#不能修的原版-bug) 的第 28 章援軍一列。

## 21. 第 17、23 章幾個沒有戰鬥畫面的單位被捲入全螢幕戰鬥時，程式結束

**現象**：下面這幾個單位只要成為全螢幕物理交戰或法術演出的一方，畫面印出找不到檔案的訊息、等一個按鍵後遊戲結束回到 DOS，存檔以外的進度全部失去：第 17 章關在 (18,23) 牢房裡的人質 `0E` 亞雷斯，第 23 章圍牆後的亡魂 `24`–`27`。平常碰不到他們，只有射程或施法距離穿過牆時才會捲進去：第 17 章（地圖 16）暗魔導士站在 (18,17) 放奔雷彈；第 23 章（地圖 22）幽魂站在 (21,4) 以靈擊打 (24,4)，或死神站在 (20,6) 對 (24,6) 放咒殺術（(21,3) 對著的 (24,3) 是亡魂的目的地，亡魂一到就退場）。AI 會不會真的站上這幾格、選這個目標，靜態分析判斷不了，沒有實機確認過。

**成因**：全螢幕的物理交戰 `fdps_combat_play_attack_exchange`（`0x18d60`）與法術演出 `fdps_combat_play_spell_on_targets`（`0x1a4c0`）以雙方（法術則是施法者與每個目標）的肖像編號組出 `Stand%03d.saf`（十進位三位數），經 `fdps_vfs_load_entry`（`0x2a140`）從 `FIGHT.VFS` 載入。成員不存在時 `fdps_vfs_load_entry` 在底層讀取器印出訊息之後等一個按鍵，接著 `exit(1)`，沒有任何退路。`FIGHT.VFS` 沒有 `STAND014` 與 `STAND036`–`STAND039`，也就是肖像編號 `0x0E` 與 `0x24`–`0x27`。戰鬥動畫關閉時，AI 發動的交戰改在地圖上結算，不載入戰鬥畫面，不會當機；玩家的施法則不看這個開關、一律走全螢幕演出（[`spell.md`](spell.md)），玩家的法術若選中這幾個單位，同樣會結束程式。容器格式見 [`../resource_info/vfs.md`](../resource_info/vfs.md)，這兩章的部署見 [`../chapters/_index.md`](../chapters/_index.md) 的第 17、23 章。

`FIGHT.VFS` 另外缺的 `STAND` 屬於正常流程不會上戰鬥畫面的單位（過場演員、MP 係數 0 而從不施法的單位），逐一的理由見 [`cut_content/_index.md`](../cut_content/_index.md) 排除清單的 U09。

**重建**：照原樣保留，載入失敗照樣結束程式，不補替代素材也不略過演出。

## 22. 第 10 章一名敵兵掉出表外物品 `FF`

**現象**：第 10 章第 19 回合部署的一批 LV40 敵兵裡，我方擊倒其中特定的一名時，訊息窗只顯示「撿到！！」、沒有物品名，背包卻多出一件物品；這件物品在物品欄裡的名稱是「裂地術」，數值不是任何道具的數值。攻略站把它叫做「裂地術」BUG 物品。

**成因**：`MAP09.DAT` 第 45 筆部署記錄的死亡腳本是 opcode 0（掉落物品）、運算元 `0xFFFF`，同一波其餘沒有腳本的記錄都是 opcode `FF`、運算元 0，這一筆把物品編號填成了 −1。`fdps_run_death_scripts`（`0x1d990`）以帶號 16-bit 讀運算元：訊息以「運算元 + `0xC9`」取物品名，得到全域文字第 `0xC8` 條的空字串；交給 `fdps_unit_add_item`（`0x25d20`）的物品編號只存低 byte，得到 `0xFF`。物品名稱照「編號 + `0xC9`」越過物品名的範圍讀到法術「裂地術」的名字（[`../assets/text/global_text.md`](../assets/text/global_text.md)），記錄則落在 `ITEM.DAT` 表尾之後，讀到的是執行期的堆積內容。死亡腳本的格式見 [`../resource_info/map.md`](../resource_info/map.md)。

這是資料填錯，不是被封住的內容，見 [`cut_content/_index.md`](../cut_content/_index.md) 排除清單的 I07。

**重建**：照原樣保留，運算元以帶號 16-bit 讀、入包只存低 byte，見 [`pitfalls.md` 的「不能換的型別與寫法」](../rebuild_info/pitfalls.md#不能換的型別與寫法) 的死亡腳本物品運算元一列；表外那一筆讀到什麼由堆積佈局決定。
