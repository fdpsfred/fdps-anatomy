# program_info — 程式視角

回答「`FDPS.LE` 現在做什麼」。一個檔對應一個子系統，之後也對應一個 `src/` 模組。

每份文件開頭有「驗證對象」段，寫明它涵蓋哪些位址範圍，該範圍內的結論以此檔為唯一正典。

| 文件 | 內容 |
| --- | --- |
| [`architecture.md`](architecture.md) | 程式架構：從 LE 進入點到 `main` 的啟動鏈、頂層迴圈與請求碼分派、各子系統進入點、共用 helper 群集與遞迴環、呼叫圖走不到的部分 |
| [`memory_layout.md`](memory_layout.md) | 位址空間：LE object table、權限、初始化資料與 BSS 的分界、近端堆積在 DPMI 記憶體上的配置方式、函式指標表與跳躍表、`.object1` 每個 byte 的歸類結果 |
| [`data_structures.md`](data_structures.md) | 資料歸屬：每個全域資料符號的身分、型別與所屬 pool，遊戲執行期記錄與程式庫記錄的欄位佈局，與前作 FD2 的逐欄對應與分歧 |
| [`code_pools.md`](code_pools.md) | 程式碼歸屬：每個 function 屬於遊戲、Watcom CRT、Miles AIL 還是連結器產物，判定依據、兩遍判定的差異與各 pool 規模，以及要還原成 C 的 `pool_fdps` 清單在哪裡；遊戲自己的手寫組語模組（鍵盤、存檔、RLE 繪製）與哪些保留原版組語 |
| [`cd_audio.md`](cd_audio.md) | CD 音源與光碟相依：MSCDEX 存取層、啟動時的光碟偵測、章節音軌對照表、影片播放外呼 |
