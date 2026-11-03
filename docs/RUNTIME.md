# 运行说明

## 启动入口

| 内容 | 版本 | 启动脚本 |
|---|---|---|
| 正常单Boss游戏及T模式 | cclassic_1102 | `05_Launch_Scripts/启动Garden_C.ps1` |
| 基础CPU／硬件同屏测试 | cbasic_min_1103 | `05_Launch_Scripts/启动基础测试_C.ps1` |
| 三Boss／2730枚弹幕 | cboss_hud_1103 | `05_Launch_Scripts/启动三Boss2730弹幕_C.ps1` |
| 六段展示 | csix_1103 | `05_Launch_Scripts/启动CPU_GPU六画面_C.ps1` |
| 六段实时测量 | csixlive_1103 | `05_Launch_Scripts/启动CPU_GPU六画面实时对比_C.ps1` |

基础同屏测试的CPU与硬件使用同一图片和运动轨迹，从2093枚开始，Alpha和Color Key效果关闭，显示实时测量值。

六段展示与六段实时测量使用不同入口；本仓库代码只保留工作负载和实时测量展示。

## 操作

两只摇杆移动，L/R慢速，A投掷，B三Boss，X进出T，Y切换CPU/GPU，+暂停，-重开；空闲后自动接管。

ESP32 TX2/GPIO17接子卡J7第40脚（GPIO6 RX），3.3V逻辑并共地，UART为115200、8N1。

正常游戏使用RAM版本；HP10 Flash安装材料与RAM加载入口分开管理，禁止混用版本。
