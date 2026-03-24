# FInal_graduate_work 宸ョ▼寮曡█涓庡紑鍙戞€绘墜鍐?
## 鏂囨。鍏冧俊鎭?
| 瀛楁 | 鍐呭 |
| --- | --- |
| 鏂囨。鍚嶇О | README |
| 鏂囨。瀹氫綅 | 椤圭洰鎬诲紩瑷€ + 鏂版墜涓婃墜鎸囧崡 + 鏋舵瀯绾︽潫瑙勮寖 + 鍗忎綔娌荤悊瑙勮寖 |
| 浣滆€?| 濮滃嚡涓?|
| 鐗堟湰 | V1.00 |
| 鏈€鍚庢洿鏂?| 2026-03-24 |
| 閫傜敤宸ョ▼ | `FInal_graduate_work` |
| 寤鸿闃呰瀵硅薄 | 瀹屽叏鏂版墜銆侀┍鍔ㄥ紑鍙戣€呫€佹ā鍧楀紑鍙戣€呫€佷换鍔＄淮鎶よ€呫€佸悗缁崗浣滆€?|

---

## 0. 闃呰璇存槑锛堝厛鐪嬶級

鏈?README 涓嶆槸鈥滅畝鍗曚粙缁嶉〉鈥濓紝鑰屾槸鏈粨搴撶殑鎬诲叆鍙ｆ枃妗ｏ紝浣滅敤鍖呮嫭锛?
1. 璁╅浂鍩虹璇昏€呰兘缂栬瘧銆佺儳褰曘€佺悊瑙ｅ苟寮€濮嬩慨鏀瑰伐绋嬨€?2. 缁?`Driver` 鍜?`Module` 寮€鍙戣€呮彁渚涘彲澶嶅埗鐨勫紑鍙戞祦绋嬨€?3. 缁欏悗缁綔鑰呮彁渚涚粺涓€鐨勭増鏈紨杩涗笌鍗忎綔瑙勫垯銆?4. 鎶婅法灞傝鍒欓泦涓浐鍖栵紝闃叉澶氫汉缁存姢鍚庢灦鏋勫け鎺с€?
濡傛灉浣犳椂闂存湁闄愶紝寤鸿鍏堟寜杩欎釜椤哄簭璇伙細

1. 绗?2 绔狅紙30 鍒嗛挓涓婃墜璺緞锛?2. 绗?4 绔狅紙鍒嗗眰杈圭晫锛?3. 绗?6 绔狅紙Driver 寮€鍙戯級
4. 绗?7 绔狅紙Module 寮€鍙戯級
5. 绗?12 绔狅紙鍗忎綔娌荤悊锛?
---

## 1. 椤圭洰鐩爣涓庢灦鏋勭悊蹇?
### 1.1 椤圭洰鐩爣

鏈伐绋嬫槸 STM32 C 椤圭洰鐨勫垎灞傚寲瀹炵幇锛屾牳蹇冪洰鏍囨槸锛?
1. 鎶婄‖浠剁粏鑺備粠涓氬姟娴佺▼涓墺绂诲嚭鏉ャ€?2. 鍦ㄥ鏉傛帶鍒朵笌閫氫俊鍏卞瓨鍦烘櫙涓嬩繚鎸佷唬鐮佸彲缁存姢銆?3. 淇濊瘉鏂板璁惧銆佸崗璁€佷换鍔℃椂鐨勬敼鍔ㄨ寖鍥村彲鎺с€?4. 璁╀笉鍚岀粡楠屾按骞崇殑寮€鍙戣€呴兘鑳藉熀浜庣粺涓€妯℃澘宸ヤ綔銆?
### 1.2 鏋舵瀯鐞嗗康

鏈伐绋嬮伒寰笁鏉′富鍘熷垯锛?
1. 鍗曞悜渚濊禆锛歚Task -> Module -> Driver -> HAL`銆?2. 鑱岃矗鍐呰仛锛氭瘡灞傚彧鍋氭湰灞傚簲鍋氱殑浜嬫儏銆?3. 鏄惧紡涓婁笅鏂囷細閫氳繃 `ctx` 绠＄悊鐘舵€佷笌鐢熷懡鍛ㄦ湡锛岄伩鍏嶉殣寮忓叏灞€鑰﹀悎銆?
### 1.3 涓轰粈涔堣繖濂楃粨鏋勫鏂版墜鍙嬪ソ

1. 鏂版墜鍙互鍙仛鐒︿竴灞傦紝涓嶅繀涓€娆℃€х悊瑙ｅ叏宸ョ▼銆?2. 姣忓眰閮芥湁缁熶竴鍛藉悕涓庢帴鍙ｈ寖寮忥紝闃呰鎴愭湰浣庛€?3. 閿欒瀹氫綅璺緞娓呮櫚锛氬厛鐪嬭皟鐢ㄥ眰锛屽啀鐪嬩笅娓稿眰銆?
---

## 2. 30 鍒嗛挓涓婃墜璺緞锛堝畬鍏ㄦ柊鎵嬶級

### 2.1 鐜鍑嗗

寤鸿鍏峰浠ヤ笅宸ュ叿锛?
1. `arm-none-eabi-gcc` 宸ュ叿閾?2. `CMake`
3. `Ninja`
4. ST-Link 涓嬭浇宸ュ叿锛堝 STM32CubeProgrammer锛?5. 涓插彛璋冭瘯宸ュ叿锛堢敤浜庤繍琛屾€佽瀵燂級

### 2.2 缂栬瘧鍛戒护

鍦ㄩ」鐩牴鐩綍鎵ц锛?
```powershell
cmake --preset Debug
cmake --build --preset Debug
```

### 2.3 浜х墿浣嶇疆

榛樿杈撳嚭鍦細

1. `build/Debug/FInal_graduate_work.elf`
2. 鍚岀洰褰曚笅鐨勫叾浠栨瀯寤轰骇鐗╋紙map 绛夛級

### 2.4 鏈€灏忚繍琛岄獙璇?
寤鸿鎸変互涓嬮『搴忛獙璇侊細

1. 涓嬭浇鎴愬姛涓旂▼搴忓彲鍚姩銆?2. 浠诲姟閾捐矾鏈夊熀鏈搷搴斻€?3. 涓插彛鐩稿叧妯″潡鍙甯哥粦瀹氥€佹敹鍙戙€佷簰鏂ャ€?4. 鏈嚭鐜版槑鏄炬寰幆銆侀樆濉炴垨寮傚父澶嶄綅銆?
---

## 3. 鐩綍鍦板浘涓庤亴璐ｅ垎鍖?
### 3.1 鏍圭洰褰曞叧閿矾寰?
1. `Code/01_Task`锛氫笟鍔′换鍔′笌璋冨害缂栨帓灞?2. `Code/02_Module`锛氭ā鍧楄涔夊皝瑁呭眰
3. `Code/03_Common`锛氶€氱敤绠楁硶涓庡伐鍏峰眰
4. `Code/04_Driver`锛氱‖浠堕┍鍔ㄦ娊璞″眰
5. `Core`锛歋TM32CubeMX 鐢熸垚涓诲共
6. `Drivers`/`Middlewares`锛氬巶鍟嗗簱鍜屼腑闂翠欢
7. `cmake` + `CMakeLists.txt` + `CMakePresets.json`锛氭瀯寤洪厤缃?
### 3.2 鏂囨。鍏ュ彛

1. Driver 灞傜粏鍒欙細`Code/04_Driver/DRIVER_LAYER_GUIDE.md`
2. Module 灞傜粏鍒欙細`Code/02_Module/MODULE_LAYER_GUIDE.md`
3. Common 灞傜粏鍒欙細`Code/03_Common/COMMON_LAYER_GUIDE.md`
4. Task 灞傜粏鍒欙細`Code/01_Task/TASK_LAYER_GUIDE.md`

鏈?README 璐熻矗鈥滄€昏鍒欏拰鎬诲叆鍙ｂ€濓紝鍥涘眰 guide 璐熻矗鈥滃眰鍐呯粏鑺傚拰鎺ュ彛璇存槑鈥濄€?
---

## 4. 鍒嗗眰杈圭晫锛堝繀椤讳弗鏍奸伒瀹堬級

### 4.1 Task 灞?
搴旇礋璐ｏ細

1. 浠诲姟璋冨害銆佺姸鎬佹満銆佹椂搴忔帶鍒?2. 璺ㄤ换鍔′俊鍙峰悓姝?3. 璋冪敤妯″潡缁勫悎涓氬姟娴佺▼

涓嶅簲璐熻矗锛?
1. 鐩存帴璋冪敤 HAL
2. 缁曡繃妯″潡鐩存帴椹卞姩纭欢
3. 娣峰叆搴曞眰鍗忚涓庡瘎瀛樺櫒缁嗚妭

### 4.2 Module 灞?
搴旇礋璐ｏ細

1. 灏?Driver 鍘熷瓙鑳藉姏缁勭粐鎴愪笟鍔¤涔?2. 缁存姢 `ctx` 鐢熷懡鍛ㄦ湡涓庤繍琛屾€佺紦瀛?3. 瀵瑰鎻愪緵楂樿涔?API

涓嶅簲璐熻矗锛?
1. RTOS 璋冨害閫昏緫
2. 浠诲姟鐘舵€佹満鎺у埗
3. 鐩存帴鎵胯浇鈥滈暱鏈熼樆濉炲瀷娴佺▼鈥?
### 4.3 Driver 灞?
搴旇礋璐ｏ細

1. 瀵瑰璁炬彁渚涙渶灏忓師瀛愯兘鍔涙帴鍙?2. 鍙傛暟/鐘舵€佹牎楠?3. 鐘舵€佺爜/閿欒鐮佽緭鍑?
涓嶅簲璐熻矗锛?
1. 涓氬姟璇箟鍒ゆ柇
2. 浠诲姟璋冨害鎴栫姸鎬佹満娴佺▼
3. 涓婂眰鍗忚瑙ｉ噴

### 4.4 Common 灞?
搴旇礋璐ｏ細

1. 涓庣‖浠舵棤鍏崇殑绠楁硶
2. 閫氱敤宸ュ叿鍑芥暟
3. 鎺у埗鍙傛暟缁熶竴瀹氫箟

涓嶅簲璐熻矗锛?
1. 澶栬璁块棶
2. RTOS 瀵硅薄绠＄悊
3. 涓庡叿浣撲笟鍔″己鑰﹀悎

---

## 5. 缁熶竴鏋舵瀯鏍稿績锛歝tx / bind / process 妯″紡

### 5.1 `ctx` 鐨勬剰涔?
`ctx`锛堜笂涓嬫枃锛夌粺涓€绠＄悊锛?
1. 鏄惁鍒濆鍖栧畬鎴愶紙`inited`锛?2. 鏄惁缁戝畾瀹屾垚锛坄bound`锛?3. 杩愯鐘舵€併€佺紦瀛樸€佺粺璁¤鏁?4. 涓庣‖浠跺彞鏌勭殑鍏宠仈鍏崇郴

### 5.2 `bind` 鐨勬剰涔?
`bind_t` 鐢ㄤ簬娉ㄥ叆鏉跨骇鏄犲皠鍜岃祫婧愶細

1. GPIO/TIM/UART 绛夊簳灞傚彞鏌?2. 鍦板潃銆佹瀬鎬с€侀檺骞呯瓑纭欢鍙傛暟
3. 骞跺彂璧勬簮锛堝 `tx_mutex`锛?
鏀剁泭锛氬悓涓€濂楁ā鍧椾唬鐮佸彲杩佺Щ鍒颁笉鍚屾澘鍗°€?
### 5.3 `process` 鐨勬剰涔?
`process` 浣滀负鍛ㄦ湡娉靛嚱鏁帮紝鎺ㄥ姩妯″潡鐘舵€佹満鍓嶈繘锛岄伩鍏嶁€滄瘡涓换鍔￠兘鍐欎竴濂楄疆璇㈤€昏緫鈥濄€?
---

## 6. Driver 灞傚紑鍙戞€绘祦绋嬶紙閲嶇偣锛?
> 鐩爣锛氭柊浜哄彲浠ユ牴鎹湰绔犵嫭绔嬪啓鍑哄彲缁存姢鐨勯┍鍔ㄣ€?
### 6.1 寮€鍙戝墠鐨勪笁闂?
寮€濮嬬紪鐮佸墠鍏堝洖绛旓細

1. 杩欎釜椹卞姩瑕佹彁渚涘摢浜涙渶灏忕‖浠惰兘鍔涳紵
2. 鍝簺鑳藉姏鏄€滃師瀛愭搷浣溾€濓紝鍝簺鏄€滀笂灞傝涔夆€濓紵
3. 閿欒璺緞濡備綍杩斿洖锛岃皟鐢ㄦ柟濡備綍鍒ゆ柇锛?
### 6.2 Driver 鎺ュ彛寤鸿缁撴瀯

寤鸿鑷冲皯鍖呭惈锛?
1. `cfg_t`锛氶潤鎬侀厤缃?2. `ctx_t`锛氳繍琛屼笂涓嬫枃
3. `ctx_init`锛氬垵濮嬪寲
4. `start/stop`锛氱敓鍛藉懆鏈熸帶鍒讹紙鐘舵€佸寲椹卞姩锛?5. `run/read/write`锛氳繍琛屾帴鍙?6. `status`锛氱姸鎬佺爜/閿欒鐮佹灇涓?
### 6.3 Driver 缂栫爜椤哄簭寤鸿

1. 鍏堝啓澶存枃浠?API锛屽啀鍐欐簮鏂囦欢瀹炵幇銆?2. 鍏堝疄鐜板弬鏁版牎楠岋紝鍐嶅疄鐜扮姸鎬佹牎楠岋紝鍐嶈皟鐢?HAL銆?3. 鍏堝疄鐜版甯歌矾寰勶紝鍐嶈ˉ澶辫触璺緞鍜屾仮澶嶉€昏緫銆?4. 鏈€鍚庤ˉ娉ㄩ噴涓庡洖褰掓祴璇曘€?
### 6.4 Driver 娉ㄩ噴鏍囧噯

1. 鏂囦欢澶村繀椤诲寘鍚綔鑰呫€佺増鏈€佹棩鏈熴€佽亴璐ｃ€?2. 姣忎釜鍏紑鍑芥暟蹇呴』鍐欏弬鏁板惈涔夈€佽繑鍥炶涔夈€佸壇浣滅敤銆?3. 澶嶆潅鍒嗘敮蹇呴』瑙ｉ噴鈥滀负浠€涔堣繖鏍峰垽鏂€濄€?
### 6.5 Driver 鍥炲綊妫€鏌ユ竻鍗?
1. 绌烘寚閽堣緭鍏ユ槸鍚﹀畨鍏ㄨ繑鍥炪€?2. 瓒婄晫鍙傛暟鏄惁鎷掔粷銆?3. `start/stop` 鏄惁骞傜瓑銆?4. 澶辫触鍚庢槸鍚﹀彲鎭㈠銆?5. 鏄惁鏈変笂灞傝涔夋薄鏌撱€?
---

## 7. Module 灞傚紑鍙戞€绘祦绋嬶紙閲嶇偣锛?
> 鐩爣锛氬熀浜庡凡鏈夐┍鍔紝蹇€熷皝瑁呬笟鍔℃ā鍧楀苟淇濇寔鍙墿灞曘€?
### 7.1 Module 鐨勮璁¤捣鐐?
鍏堝畾涔夆€滀笟鍔¤涔夆€濊€屼笉鏄€滃簳灞傜粏鑺傗€濓紝渚嬪锛?
1. LED 妯″潡鍏虫敞鈥滃紑/鍏?闂儊鈥?2. 鐢垫満妯″潡鍏虫敞鈥滄ā寮?鍗犵┖姣?閫熷害/浣嶇疆鈥?3. 鍗忚妯″潡鍏虫敞鈥滃抚杈撳叆/甯ц緭鍑?鐘舵€佲€?
### 7.2 Module 鏍囧噯鎺ュ彛楠ㄦ灦

寤鸿缁熶竴鎻愪緵锛?
1. `mod_xxx_get_default_ctx`
2. `mod_xxx_ctx_init`
3. `mod_xxx_bind`
4. `mod_xxx_unbind`
5. `mod_xxx_is_bound`
6. 杩愯鏈熻涔?API锛坄set/get/update/process`锛?
### 7.3 Module 缂栫爜椤哄簭寤鸿

1. 瀹氫箟 `bind_t` 涓?`ctx_t`銆?2. 瀹炵幇鐢熷懡鍛ㄦ湡鎺ュ彛銆?3. 瀹炵幇杩愯鏈熸帴鍙ｃ€?4. 鎺ュ叆 InitTask 鍒濆鍖栭摼璺€?5. 鍦ㄤ换鍔″眰鏇挎崲涓烘ā鍧楄涔夎皟鐢ㄣ€?
### 7.4 Module 閿欒绛栫暐

妯″潡蹇呴』鍐冲畾澶辫触鍚庣殑鍔ㄤ綔锛岃€屼笉鏄崟绾€忎紶锛?
1. 鍙噸璇曪細鍦ㄦā鍧楀唴鏈夐檺娆￠噸璇曘€?2. 涓嶅彲閲嶈瘯锛氬揩閫熻繑鍥炲苟涓婃姤鐘舵€併€?3. 蹇呴』闄嶇骇锛氭竻鐘舵€佸苟杩涘叆瀹夊叏妯″紡銆?
### 7.5 Module 鍥炲綊妫€鏌ユ竻鍗?
1. 鏈粦瀹氳皟鐢ㄦ槸鍚﹀畨鍏ㄣ€?2. 閲嶅 bind/unbind 鏄惁瀹夊叏銆?3. 杩愯鏈熺粺璁″瓧娈垫槸鍚︽纭洿鏂般€?4. 瀵?Task 灞?API 鏄惁绠€娲佺ǔ瀹氥€?
---

## 8. UART 閫氶亾缁熶竴鏋舵瀯璇存槑锛堝叧閿級

鏈伐绋嬬殑 UART 鐩稿叧妯″潡锛堝 `mod_vofa/mod_k230/mod_stepper`锛夐噰鐢ㄧ粺涓€绛栫暐锛?
1. 涓插彛纭欢鎿嶄綔缁熶竴璧?`drv_uart`銆?2. 涓插彛鍗犵敤缁熶竴璧?`mod_uart_guard`銆?3. 涓插彛鍙戦€佸苟鍙戠粺涓€璧?`tx_mutex`銆?4. 鍥炶皟涓婁笅鏂囩粺涓€璧?`user_ctx`銆?
### 8.1 涓轰粈涔堥渶瑕?`mod_uart_guard`

澶氭ā鍧楀叡浜覆鍙ｆ椂锛屾渶甯歌闂鏄€滀簰鐩歌鐩栧洖璋冩垨鎶㈠崰鍙戦€佲€濄€? 
`mod_uart_guard` 鐨勭洰鏍囧氨鏄涓插彛褰掑睘鏄惧紡鍖栵紝閬垮厤闅愬紡鍐茬獊銆?
### 8.2 涓轰粈涔堣繕瑕?`tx_mutex`

`guard` 绠＄殑鏄€滃綊灞炩€濓紝`mutex` 绠＄殑鏄€滃悓涓€鏃跺埢鍙戦€佷簰鏂モ€濓紝涓よ€呯己涓€涓嶅彲銆?
### 8.3 缁熶竴鎺ュ叆瑕佹眰

鏂板 UART 鍗忚妯″潡鏃讹紝蹇呴』鍚屾椂婊¤冻锛?
1. 缁戝畾闃舵瀹屾垚 claim
2. 瑙ｇ粦闃舵瀹屾垚 release
3. 鍙戦€佽矾寰勭粺涓€鍔犱簰鏂?4. 鍥炶皟娉ㄥ唽/娉ㄩ攢鎴愬鍑虹幇

---

## 9. 鏂颁汉鎵╁睍瀹炴垬璺嚎锛堟寜杩欎釜鍋氬嚑涔庝笉浼氳窇鍋忥級

### 9.1 璺嚎 A锛氬厛鍐欎竴涓柊 Driver

1. 鏂板缓 `drv_xxx.h/.c`
2. 瀹炵幇 `ctx` 涓庣敓鍛藉懆鏈?3. 瀹炵幇鏈€灏忚繍琛屾帴鍙?4. 鍔犲叆 `CMakeLists.txt`
5. 缂栬瘧閫氳繃 + 鍩虹鑱旇皟

### 9.2 璺嚎 B锛氬啀鍐欎竴涓柊 Module

1. 鏂板缓 `mod_xxx.h/.c`
2. 娉ㄥ叆 `bind_t`
3. 绠＄悊 `ctx` 鐘舵€?4. 灏佽璇箟 API
5. 鍦?InitTask 鎺ュ叆鍒濆鍖?
### 9.3 璺嚎 C锛氭渶鍚庢帴浠诲姟灞?
1. 浠诲姟涓彧璋冪敤妯″潡鎺ュ彛
2. 涓嶈法灞傜洿杈?Driver
3. 缁欎换鍔＄姸鎬佸揩鐓цˉ瀛楁
4. 鍋氭渶灏忓洖褰掗獙璇?
---

## 10. 璋冭瘯鏂规硶璁猴紙鐜拌薄鍒版牴鍥狅級

### 10.1 缂栬瘧绫婚棶棰?
鐜拌薄锛氶摼鎺ユ姤鏈畾涔夌鍙枫€? 
妫€鏌ワ細

1. 鏄惁鎶婃柊 `.c` 鍔犲埌 `CMakeLists.txt`
2. 澶存枃浠跺０鏄庢槸鍚︿笌瀹氫箟涓€鑷?3. include 璺緞鏄惁姝ｇ‘

### 10.2 鐢熷懡鍛ㄦ湡闂

鐜拌薄锛氬嚱鏁拌繑鍥炵姸鎬侀敊璇絾閫昏緫鐪嬩技姝ｇ‘銆? 
妫€鏌ワ細

1. `ctx_init` 鏄惁鎵ц
2. `bind` 鏄惁鎴愬姛
3. `start` 鏄惁琚皟鐢?
### 10.3 骞跺彂闂

鐜拌薄锛氫覆鍙ｅ伓鍙戝紓甯搞€佹暟鎹敊甯с€佸彂閫佸啿绐併€? 
妫€鏌ワ細

1. 鏄惁婕忓姞浜掓枼
2. 鏄惁澶氭ā鍧椾簤鎶㈠悓涓€涓插彛
3. 鍥炶皟鏄惁琚噸澶嶈鐩?
### 10.4 鎺у埗闂

鐜拌薄锛氳緭鍑烘棤鏁堟垨鎶栧姩銆? 
妫€鏌ワ細

1. 妯″紡鍒嗘敮鏄惁鍛戒腑
2. 姝诲尯鍜岄檺骞呮槸鍚﹁繃绱?3. 杈撳叆閲忔槸鍚︽纭洿鏂?
---

## 11. 璐ㄩ噺闂ㄧ涓庨獙鏀舵爣鍑?
### 11.1 鏈€浣庝氦浠樻爣鍑?
1. Debug 缂栬瘧閫氳繃
2. 涓嶅紩鍏ユ柊鐨勮法灞傝繚瑙勮皟鐢?3. 鐢熷懡鍛ㄦ湡璺緞瀹屾暣
4. 鏂囨。鍚屾鏇存柊

### 11.2 寤鸿闄勫姞楠岃瘉

1. 寮傚父杈撳叆娴嬭瘯
2. 楂樿礋杞藉懆鏈熺ǔ瀹氭€ф祴璇?3. 鍏抽敭閫氫俊閾捐矾杩炵画杩愯娴嬭瘯
4. 鍏抽敭浠诲姟闀挎湡杩愯绋冲畾鎬ц瀵?
### 11.3 鍥炲綊鐭╅樀寤鸿

| 缁村害 | 妫€鏌ョ偣 | 缁撴灉璁板綍 |
| --- | --- | --- |
| 缂栬瘧 | Debug/Release 鍧囧彲鏋勫缓 | 閫氳繃/澶辫触 |
| 鍔熻兘 | 鏂板鍔熻兘涓昏矾寰?| 閫氳繃/澶辫触 |
| 寮傚父 | 绌烘寚閽堛€佹湭缁戝畾銆佽秺鐣?| 閫氳繃/澶辫触 |
| 骞跺彂 | 涓插彛浜掓枼涓庡綊灞?| 閫氳繃/澶辫触 |
| 鏂囨。 | README + 瀵瑰簲灞?guide | 宸叉洿鏂?鏈洿鏂?|

---

## 12. 澶氫綔鑰呭崗浣滄不鐞嗭紙鍚庣画鎵╁睍鏍稿績锛?
> 鏈珷鐢ㄤ簬淇濊瘉鈥滃悗缁増鏈寔缁紨杩涗絾鏋舵瀯涓嶅け鎺р€濄€?
### 12.1 鐗堟湰鍙疯鍒?
閲囩敤 `V涓荤増鏈?娆＄増鏈琡锛?
1. 鐮村潖鎬у彉鏇达細`Vx.yy -> V(x+1).00`
2. 鍏煎鎬у寮猴細`Vx.yy -> Vx.(yy+1)`

### 12.2 浣滆€呯櫥璁拌〃锛堥暱鏈熺淮鎶わ級

鍒濆鐧昏锛?
| 浣滆€?| 瑙掕壊 | 璐熻矗鑼冨洿 | 棣栨鍙備笌鐗堟湰 | 鐘舵€?|
| --- | --- | --- | --- | --- |
| 濮滃嚡涓?| 鏋舵瀯缁存姢鑰?| 鍏ㄥ眬 | V1.00 | Active |

鍚庣画浣滆€呭姞鍏ユ椂鏂板琛岋紝涓嶈鐩栧巻鍙层€?
### 12.3 鍙樻洿璁板綍妯℃澘

```markdown
### [Vx.yy] - YYYY-MM-DD - 浣滆€呭悕
1. 鍙樻洿鐩爣锛?2. 褰卞搷灞傜骇锛?3. 鍏煎鎬у奖鍝嶏細
4. 楠岃瘉鏂瑰紡涓庣粨鏋滐細
5. 閬楃暀浜嬮」锛?
```

### 12.4 鎺ュ彛婕旇繘绛栫暐

1. 浼樺厛鏂板鎺ュ彛锛屼笉鏀规棫璇箟銆?2. 蹇呴』鏀硅涔夋椂鍏堟彁渚涘吋瀹瑰眰銆?3. 鏍囨敞搴熷純鎺ュ彛鐨勭Щ闄よ鍒掔増鏈€?
### 12.5 鏂囨。娌荤悊绛栫暐

1. 鏍?README 缁存姢璺ㄥ眰瑙勫垯涓庢€诲叆鍙ｃ€?2. 姣忓眰浠呯淮鎶や竴涓眰绾?guide 鏂囨。銆?3. 瑙勫垯鍙樻洿蹇呴』鏂囨。涓庝唬鐮佸悓姝ユ洿鏂般€?4. PR 鏈洿鏂版枃妗ｈ涓烘湭瀹屾垚銆?
---

## 13. 璐＄尞娴佺▼寤鸿锛堥€傚悎鍥㈤槦锛?
### 13.1 鍒嗘敮鍛藉悕寤鸿

1. `feature/driver-xxx`
2. `feature/module-xxx`
3. `refactor/uart-stack`
4. `docs/readme-v101`

### 13.2 Commit 寤鸿鏍煎紡

```text
layer(scope): summary
```

绀轰緥锛?
1. `driver(uart): add rx event callback user_ctx`
2. `module(stepper): split ff and pid control path`
3. `docs(readme): add contributor governance rules`

### 13.3 PR 鎻忚堪妯℃澘

```markdown
## 鍙樻洿鐩殑

## 鍙樻洿鑼冨洿
- [ ] Driver
- [ ] Module
- [ ] Common
- [ ] Task
- [ ] Docs

## 鍏煎鎬?- [ ] 鍚戝悗鍏煎
- [ ] 鐮村潖鎬у彉鏇达紙宸叉彁渚涜縼绉昏鏄庯級

## 楠岃瘉缁撴灉
- [ ] 缂栬瘧閫氳繃
- [ ] 鍔熻兘鍥炲綊閫氳繃
- [ ] 寮傚父璺緞楠岃瘉閫氳繃

## 鏂囨。鍚屾
- [ ] 宸叉洿鏂板搴斿眰 guide
- [ ] 宸叉洿鏂?README锛堟秹鍙婅法灞傝鍒欐椂锛?
```

---

## 14. 甯歌璇尯锛堣閬垮厤锛?
1. 鍦?Task 灞傜洿鎺ユ搷浣滃簳灞傚璁俱€?2. 鍦?Driver 灞傚姞鍏ヤ笟鍔＄姸鎬佹満銆?3. 鍦?Module 灞傞暱鏈熼樆濉炵瓑寰呬簨浠躲€?4. 鍦ㄦ湭 bind 鐘舵€佺洿鎺ュ彂鍛戒护銆?5. 澶氭ā鍧楀叡浜覆鍙ｄ絾涓嶄娇鐢?guard/mutex銆?6. 鍙敼浠ｇ爜涓嶆敼鏂囨。銆?
---

## 15. 鏂颁汉 7 澶╄缁冭鍒掞紙寤鸿锛?
### Day 1

1. 缂栬瘧骞剁儳褰?2. 鐞嗚В鐩綍涓庡垎灞傝竟鐣?
### Day 2

1. 閫氳 Driver 澶存枃浠?2. 鐢诲嚭甯哥敤璋冪敤閾?
### Day 3

1. 閫氳 Module 鐢熷懡鍛ㄦ湡鎺ュ彛
2. 璺熻釜涓€涓畬鏁翠笟鍔￠摼璺?
### Day 4

1. 鏂板缓 `drv_demo` 楠ㄦ灦
2. 瀹屾垚鍙傛暟涓庣姸鎬佹牎楠?
### Day 5

1. 鏂板缓 `mod_demo` 楠ㄦ灦
2. 鎺ュ叆鍒濆鍖栭摼璺?
### Day 6

1. 鍦ㄤ换鍔″眰鍋氭渶灏忔帴鍏?2. 瀹屾垚寮傚父璺緞楠岃瘉

### Day 7

1. 杈撳嚭鍙樻洿璁板綍
2. 琛ラ綈鏂囨。鍜岃瘎瀹℃竻鍗?
---

## 16. 鏈琛?
1. `ctx`锛氳繍琛屼笂涓嬫枃瀵硅薄
2. `bind`锛氱‖浠舵槧灏勪笌璧勬簮娉ㄥ叆
3. `inited`锛氬垵濮嬪寲瀹屾垚鏍囧織
4. `bound`锛氱粦瀹氬畬鎴愭爣蹇?5. `process`锛氬懆鏈熸车鍑芥暟
6. `guard`锛氳祫婧愬綊灞炰徊瑁?7. `tx_mutex`锛氬彂閫佷簰鏂ラ攣
8. `骞傜瓑`锛氶噸澶嶈皟鐢ㄤ笉浜х敓棰濆鍓綔鐢?
---

## 17. 闄勫綍 A锛欴river 妯℃澘锛堝彲澶嶅埗锛?
### A.1 澶存枃浠舵ā鏉?
```c
/**
 * @file    drv_foo.h
 * @author  濮滃嚡涓? * @version V1.00
 * @date    2026-03-24
 * @brief   Foo 澶栬椹卞姩鎺ュ彛銆? */
#ifndef FINAL_GRADUATE_WORK_DRV_FOO_H
#define FINAL_GRADUATE_WORK_DRV_FOO_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    DRV_FOO_OK = 0,
    DRV_FOO_ERR_PARAM,
    DRV_FOO_ERR_STATE,
    DRV_FOO_ERR_HAL
} drv_foo_status_t;

typedef struct
{
    void *hw;
    uint32_t timeout_ms;
} drv_foo_cfg_t;

typedef struct
{
    bool inited;
    bool started;
    drv_foo_cfg_t cfg;
} drv_foo_ctx_t;

drv_foo_status_t drv_foo_ctx_init(drv_foo_ctx_t *ctx, const drv_foo_cfg_t *cfg);
drv_foo_status_t drv_foo_start(drv_foo_ctx_t *ctx);
drv_foo_status_t drv_foo_stop(drv_foo_ctx_t *ctx);
drv_foo_status_t drv_foo_read(drv_foo_ctx_t *ctx, uint32_t *out_value);

#endif
```

### A.2 婧愭枃浠舵ā鏉?
```c
#include "drv_foo.h"
#include <string.h>

drv_foo_status_t drv_foo_ctx_init(drv_foo_ctx_t *ctx, const drv_foo_cfg_t *cfg)
{
    if ((ctx == NULL) || (cfg == NULL) || (cfg->hw == NULL))
    {
        return DRV_FOO_ERR_PARAM;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->cfg = *cfg;
    ctx->inited = true;
    return DRV_FOO_OK;
}

drv_foo_status_t drv_foo_start(drv_foo_ctx_t *ctx)
{
    if ((ctx == NULL) || (!ctx->inited))
    {
        return DRV_FOO_ERR_STATE;
    }
    if (ctx->started)
    {
        return DRV_FOO_OK;
    }
    ctx->started = true;
    return DRV_FOO_OK;
}

drv_foo_status_t drv_foo_stop(drv_foo_ctx_t *ctx)
{
    if ((ctx == NULL) || (!ctx->inited))
    {
        return DRV_FOO_ERR_STATE;
    }
    if (!ctx->started)
    {
        return DRV_FOO_OK;
    }
    ctx->started = false;
    return DRV_FOO_OK;
}

drv_foo_status_t drv_foo_read(drv_foo_ctx_t *ctx, uint32_t *out_value)
{
    if ((ctx == NULL) || (out_value == NULL))
    {
        return DRV_FOO_ERR_PARAM;
    }
    if ((!ctx->inited) || (!ctx->started))
    {
        return DRV_FOO_ERR_STATE;
    }
    *out_value = 0;
    return DRV_FOO_OK;
}
```

---

## 18. 闄勫綍 B锛歁odule 妯℃澘锛堝彲澶嶅埗锛?
### B.1 澶存枃浠舵ā鏉?
```c
/**
 * @file    mod_foo.h
 * @author  濮滃嚡涓? * @version V1.00
 * @date    2026-03-24
 * @brief   Foo 妯″潡璇箟鎺ュ彛銆? */
#ifndef FINAL_GRADUATE_WORK_MOD_FOO_H
#define FINAL_GRADUATE_WORK_MOD_FOO_H

#include "drv_foo.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    void *hw;
} mod_foo_bind_t;

typedef struct
{
    bool inited;
    bool bound;
    drv_foo_ctx_t drv;
} mod_foo_ctx_t;

mod_foo_ctx_t *mod_foo_get_default_ctx(void);
bool mod_foo_ctx_init(mod_foo_ctx_t *ctx, const mod_foo_bind_t *bind);
bool mod_foo_bind(mod_foo_ctx_t *ctx, const mod_foo_bind_t *bind);
void mod_foo_unbind(mod_foo_ctx_t *ctx);
bool mod_foo_is_bound(const mod_foo_ctx_t *ctx);
bool mod_foo_update(mod_foo_ctx_t *ctx);

#endif
```

### B.2 婧愭枃浠舵ā鏉?
```c
#include "mod_foo.h"
#include <string.h>

static mod_foo_ctx_t s_foo;

mod_foo_ctx_t *mod_foo_get_default_ctx(void)
{
    return &s_foo;
}

bool mod_foo_bind(mod_foo_ctx_t *ctx, const mod_foo_bind_t *bind)
{
    drv_foo_cfg_t cfg;
    if ((ctx == NULL) || (bind == NULL) || (bind->hw == NULL))
    {
        return false;
    }
    memset(&cfg, 0, sizeof(cfg));
    cfg.hw = bind->hw;
    cfg.timeout_ms = 10U;
    if (drv_foo_ctx_init(&ctx->drv, &cfg) != DRV_FOO_OK)
    {
        return false;
    }
    if (drv_foo_start(&ctx->drv) != DRV_FOO_OK)
    {
        return false;
    }
    ctx->bound = true;
    return true;
}

bool mod_foo_ctx_init(mod_foo_ctx_t *ctx, const mod_foo_bind_t *bind)
{
    if (ctx == NULL)
    {
        return false;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->inited = true;
    if (bind != NULL)
    {
        return mod_foo_bind(ctx, bind);
    }
    return true;
}

void mod_foo_unbind(mod_foo_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }
    (void)drv_foo_stop(&ctx->drv);
    ctx->bound = false;
}

bool mod_foo_is_bound(const mod_foo_ctx_t *ctx)
{
    return (ctx != NULL) && ctx->bound;
}

bool mod_foo_update(mod_foo_ctx_t *ctx)
{
    uint32_t value;
    if (!mod_foo_is_bound(ctx))
    {
        return false;
    }
    return (drv_foo_read(&ctx->drv, &value) == DRV_FOO_OK);
}
```

---

## 19. 闄勫綍 C锛氳縼绉昏鏄庢ā鏉匡紙鐮村潖鎬ф敼鍔ㄦ椂蹇呭～锛?
```markdown
## 杩佺Щ椤?1. 鏃ф帴鍙ｏ細
2. 鏂版帴鍙ｏ細
3. 杩佺Щ姝ラ锛?4. 椋庨櫓鐐癸細
5. 鍥炴粴鏂规锛?
```

---

## 20. 鐗堟湰璁板綍

### V1.00锛?026-03-24锛?
1. 鏂板缓鏍?README锛屽缓绔嬮」鐩€诲叆鍙ｃ€?2. 缁熶竴璺ㄥ眰杈圭晫涓庤В鑰﹁鍒欍€?3. 鎻愪緵 Driver/Module 鐨勫彲澶嶅埗寮€鍙戞祦绋嬨€?4. 鎻愪緵澶氫汉鍗忎綔鐨勭増鏈不鐞嗕笌鏂囨。娌荤悊瑙勮寖銆?5. 鎻愪緵妯℃澘闄勫綍锛屾敮鎸佸悗缁綔鑰呭揩閫熸墿灞曘€?
---

## 21. 褰撳墠浠诲姟娓呭崟锛堝己鍒跺悓姝ュ尯锛?
> 缁存姢寮哄埗椤癸細褰撲綘淇敼浠诲姟鍏ュ彛銆佷紭鍏堢骇銆佹爤澶у皬銆佷换鍔¤亴璐ｆ垨鍚屾璧勬簮鏃讹紝蹇呴』鍚屾鏇存柊鏈珷銆? 
> 鏈洿鏂版湰绔狅紝瑙嗕负鏂囨。鏈畬鎴愩€?
### 21.1 绾跨▼閰嶇疆鎬昏〃锛堟潵婧愶細`Core/Src/freertos.c`锛?
| 浠诲姟鍚?| 鍏ュ彛鍑芥暟 | 鏍堥厤缃?| 鏍堝瓧鑺?| 浼樺厛绾?| 鐢熷懡鍛ㄦ湡 | 褰撳墠鑱岃矗鎽樿 |
| --- | --- | --- | ---: | --- | --- | --- |
| `defaultTask` | `StartDefaultTask` | `128*4` | `512` | `osPriorityLow` | 甯搁┗ | 淇濆簳绌鸿浆浠诲姟锛屽惎鍔ㄥ悗绛夊緟 Init 闂搁棬 |
| `GpioTask` | `StartGpioTask` | `128*4` | `512` | `osPriorityLow` | 甯搁┗ | LED/铚傞福鍣?婵€鍏夎緭鍑轰徊瑁?|
| `KeyTask` | `StartKeyTask` | `128*4` | `512` | `osPriorityLow` | 甯搁┗ | 鎸夐敭鎵弿涓庝簨浠跺垎鍙?|
| `OledTask` | `StartOledTask` | `128*4` | `512` | `osPriorityLow` | 甯搁┗ | 鐢靛帇閲囨牱涓?OLED 椤甸潰鍒锋柊 |
| `TestTask` | `StartTestTask` | `128*4` | `512` | `osPriorityHigh` | 甯搁┗ | 鑱旇皟棰勭暀浠诲姟 |
| `StepperTask` | `StartStepperTask` | `1024*4` | `4096` | `osPriorityRealtime` | 甯搁┗ | 瑙嗚璇樊椹卞姩鐨勬杩涘弻杞存帶鍒?|
| `DccTask` | `StartDccTask` | `512*4` | `2048` | `osPriorityRealtime` | 甯搁┗ | 搴曠洏妯″紡涓庤繍琛岀姸鎬佹満鎺у埗 |
| `InitTask` | `StartInitTask` | `256*4` | `1024` | `osPriorityRealtime7` | 涓€娆℃€?| 妯″潡缁戝畾銆佸垵濮嬪寲銆侀噴鏀惧惎鍔ㄩ椄闂ㄥ悗鑷垹闄?|

### 21.2 鍚屾璧勬簮鎬昏〃锛堟潵婧愶細`Core/Src/freertos.c`锛?
| 璧勬簮鍚?| 绫诲瀷 | 鍒濆鍊?| 褰撳墠鐢ㄩ€?| 涓昏鐢熶骇鑰?| 涓昏娑堣垂鑰?|
| --- | --- | ---: | --- | --- | --- |
| `PcMutexHandle` | Mutex | N/A | 涓插彛鍙戦€佷簰鏂ヤ繚鎶?| InitTask 娉ㄥ叆鍒扮粦瀹氬弬鏁?| VOFA/Stepper 绛変覆鍙ｆā鍧?|
| `Sem_RedLEDHandle` | Semaphore | 0 | 鎸夐敭鍙嶉瑙﹀彂鐏晥/铚傞福 | KeyTask | GpioTask |
| `Sem_DccHandle` | Semaphore | 0 | 瑙﹀彂 DCC 鐩稿叧鍔ㄤ綔 | KeyTask | DccTask |
| `Sem_TaskChangeHandle` | Semaphore | 0 | 鍒囨崲 DCC 妯″紡 | KeyTask | DccTask |
| `Sem_InitHandle` | Semaphore | 0 | 鍒濆鍖栭椄闂?| InitTask | 鎵€鏈変笟鍔′换鍔?|
| `Sem_ReadyToggleHandle` | Semaphore | 0 | 鍒囨崲 Ready/Run 鐩稿叧鐘舵€?| KeyTask | DccTask |

### 21.3 浠诲姟閰嶇疆鏀瑰姩鍚庣殑蹇呮敼鏂囦欢娓呭崟

1. `Core/Src/freertos.c`锛氱嚎绋?淇″彿閲忓畾涔変笌鍒涘缓鍙傛暟銆?2. `Code/01_Task/*.h/.c`锛氫换鍔″叆鍙ｈ涓轰笌娉ㄩ噴銆?3. `Code/01_Task/TASK_LAYER_GUIDE.md`锛氫换鍔″眰閰嶇疆鎬昏〃銆?4. `README.md` 绗?21 绔狅細褰撳墠浠诲姟娓呭崟銆?
---

## 22. 褰撳墠寮曡剼涓庡璁炬槧灏勶紙寮哄埗鍚屾鍖猴級

> 缁存姢寮哄埗椤癸細褰撲綘鏀瑰姩浠讳綍寮曡剼銆佸璁惧疄渚嬨€丏MA 缁戝畾銆佹ā鍧楃粦瀹氬叧绯绘椂锛屽繀椤诲悓姝ユ洿鏂版湰绔犮€? 
> 鏈洿鏂版湰绔狅紝瑙嗕负纭欢鏂囨。鏈畬鎴愩€?
### 22.1 GPIO 鍔熻兘寮曡剼鎬昏〃锛堟潵婧愶細`Core/Inc/main.h`锛?
| 鍔熻兘 | 绔彛寮曡剼 | 褰撳墠鐢ㄩ€?|
| --- | --- | --- |
| `Laser_Pin` | `PE2` | 婵€鍏夌户鐢靛櫒鎺у埗 |
| `Buzzer_Pin` | `PE3` | 铚傞福鍣ㄧ户鐢靛櫒鎺у埗 |
| `LED_RED_Pin` | `PF10` | 绾㈢伅 |
| `LED_GREEN_Pin` | `PF11` | 缁跨伅 |
| `LED_YELLOW_Pin` | `PF12` | 榛勭伅 |
| `AIN1_Pin` | `PE7` | 宸︾數鏈烘柟鍚戞帶鍒剁嚎 |
| `AIN2_Pin` | `PE8` | 宸︾數鏈烘柟鍚戞帶鍒剁嚎 |
| `BIN1_Pin` | `PE9` | 鍙崇數鏈烘柟鍚戞帶鍒剁嚎 |
| `BIN2_Pin` | `PE10` | 鍙崇數鏈烘柟鍚戞帶鍒剁嚎 |
| `KEY_1_Pin` | `PG2` | 鎸夐敭 1 杈撳叆 |
| `KEY_2_Pin` | `PG3` | 鎸夐敭 2 杈撳叆 |
| `KEY_3_Pin` | `PG4` | 鎸夐敭 3 杈撳叆 |
| `Sensor_1_Pin` | `PG0` | 寰抗閫氶亾 1 |
| `Sensor_2_Pin` | `PG1` | 寰抗閫氶亾 2 |
| `Sensor_3_Pin` | `PG5` | 寰抗閫氶亾 3 |
| `Sensor_4_Pin` | `PG6` | 寰抗閫氶亾 4 |
| `Sensor_5_Pin` | `PG7` | 寰抗閫氶亾 5 |
| `Sensor_6_Pin` | `PG8` | 寰抗閫氶亾 6 |
| `Sensor_7_Pin` | `PG9` | 寰抗閫氶亾 7 |
| `Sensor_8_Pin` | `PG10` | 寰抗閫氶亾 8 |
| `Sensor_9_Pin` | `PG11` | 寰抗閫氶亾 9 |
| `Sensor_10_Pin` | `PG12` | 寰抗閫氶亾 10 |
| `Sensor_11_Pin` | `PG13` | 寰抗閫氶亾 11 |
| `Sensor_12_Pin` | `PG14` | 寰抗閫氶亾 12 |

### 22.2 閫氫俊涓庢€荤嚎寮曡剼琛紙鏉ユ簮锛歚Core/Src/usart.c`銆乣Core/Src/i2c.c`锛?
| 澶栬 | TX/SDA | RX/SCL | 榛樿娉㈢壒鐜?鏃堕挓 | 涓昏妯″潡缁戝畾 |
| --- | --- | --- | --- | --- |
| `UART4` | `PC10` | `PA1` | `115200` | `mod_k230` |
| `UART5` | `PC12` | `PD2` | `115200` | `Stepper X 杞碻 |
| `USART2` | `PD5` | `PD6` | `115200` | `Stepper Y 杞碻 |
| `USART3` | `PB10` | `PB11` | `115200` | `mod_vofa` |
| `I2C2` | `PF0 (SDA)` | `PF1 (SCL)` | `400kHz` | `OLED` |

### 22.3 鐢垫満銆佺紪鐮佸櫒銆丄DC 寮曡剼琛紙鏉ユ簮锛歚Core/Src/tim.c`銆乣Core/Src/adc.c`銆乣Code/01_Task/Src/task_init.c`锛?
| 鍔熻兘 | 澶栬/閫氶亾 | 寮曡剼 | 褰撳墠妯″潡鐢ㄩ€?|
| --- | --- | --- | --- |
| 宸︾數鏈?PWM | `TIM4_CH1` | `PD12` | `mod_motor` 宸︾數鏈哄崰绌烘瘮杈撳嚭 |
| 鍙崇數鏈?PWM | `TIM4_CH2` | `PD13` | `mod_motor` 鍙崇數鏈哄崰绌烘瘮杈撳嚭 |
| 宸︾紪鐮佸櫒 | `TIM2_CH1/CH2` | `PA15/PB3` | `mod_motor` 宸﹁疆缂栫爜鍣ㄨ緭鍏?|
| 鍙崇紪鐮佸櫒 | `TIM3_CH1/CH2` | `PB4/PB5` | `mod_motor` 鍙宠疆缂栫爜鍣ㄨ緭鍏?|
| 鐢垫睜 ADC | `ADC1_IN2` | `PA2` | `mod_battery` 鐢靛帇閲囨牱杈撳叆 |

### 22.4 妯″潡缁戝畾鍏崇郴鎬昏〃锛堟潵婧愶細`Code/01_Task/Src/task_init.c`銆乣Code/01_Task/Src/task_stepper.c`锛?
| 妯″潡/瀵硅薄 | 缁戝畾瀹炰緥 | 鍏抽敭缁戝畾鍙傛暟 |
| --- | --- | --- |
| `mod_vofa` | `huart3` | `tx_mutex = PcMutexHandle` |
| `mod_k230` | `huart4` | `checksum = XOR` |
| `Stepper X` | `huart5` | `driver_addr = 1` |
| `Stepper Y` | `huart2` | `driver_addr = 1` |
| `mod_battery` | `hadc1` | 榛樿鍙傝€冪數鍘?鍒嗗帇姣斿弬鏁?|
| `OLED` | `hi2c2` | `OLED_BindI2C(..., default_addr, default_timeout)` |
| 宸︾數鏈?| `TIM4_CH1 + TIM2` | 鏂瑰悜鑴氾細`AIN2/AIN1` |
| 鍙崇數鏈?| `TIM4_CH2 + TIM3` | 鏂瑰悜鑴氾細`BIN1/BIN2` |

### 22.5 12 璺惊杩规潈閲嶈〃锛堟潵婧愶細`Code/01_Task/Src/task_init.c`锛?
| 閫氶亾 | 寮曡剼 | 鏉冮噸 |
| --- | --- | ---: |
| 1 | `PG0` | `0.60` |
| 2 | `PG1` | `0.40` |
| 3 | `PG5` | `0.30` |
| 4 | `PG6` | `0.20` |
| 5 | `PG7` | `0.10` |
| 6 | `PG8` | `0.05` |
| 7 | `PG9` | `-0.05` |
| 8 | `PG10` | `-0.10` |
| 9 | `PG11` | `-0.20` |
| 10 | `PG12` | `-0.30` |
| 11 | `PG13` | `-0.40` |
| 12 | `PG14` | `-0.60` |

### 22.6 寮曡剼鏀瑰姩鍚庣殑蹇呮敼鏂囦欢娓呭崟

1. `Core/Inc/main.h`锛欸PIO 瀹忓畾涔夈€?2. `Core/Src/usart.c`锛歎ART 寮曡剼銆丏MA銆佷腑鏂厤缃€?3. `Core/Src/tim.c`锛歅WM/缂栫爜鍣ㄩ€氶亾涓?AF 寮曡剼銆?4. `Core/Src/adc.c`锛欰DC 閫氶亾涓庢ā鎷熻緭鍏ュ紩鑴氥€?5. `Core/Src/i2c.c`锛欼2C 寮曡剼涓庨€熺巼銆?6. `Code/01_Task/Src/task_init.c`锛氭ā鍧楃粦瀹氭槧灏勮〃銆?7. `Code/01_Task/Src/task_stepper.c`锛歋tepper 涓插彛缁戝畾鏄犲皠銆?8. `README.md` 绗?22 绔狅細褰撳墠寮曡剼涓庣粦瀹氭竻鍗曘€?

