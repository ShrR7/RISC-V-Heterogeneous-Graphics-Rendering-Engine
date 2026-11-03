# Ti60F225 Garden Graphics Accelerator

RISC-V 与 FPGA 协同的二维图形加速弹幕游戏机。仓库只保留工程源码、运行素材、固件、构建配置和简要说明，不包含竞赛报告。

## 功能

- FPGA 图形加速：图像块复制、纯色填充、Color Key、Alpha 混合和按行合成。
- AXI4/DDR3 存储访问、双帧缓冲和 HDMI 1280×720 输出。
- 普通敌机与 Boss 战、性能对照演示、蓝牙双 Joy-Con、Flash 自启动。
- 演示界面展示实际工作负载和测量结果，测量数据按场景与版本分别记录。

## 目录

| 目录 | 内容 |
|---|---|
| `01_Hardware_Platform` | Ti60硬件平台、游戏源码、BSP、素材和构建输入 |
| `02_FPGA_Accelerator` | FPGA配置、同批源码和硬件构建配置 |
| `03_Software_Firmware` | Flash负载、加载器、ESP32固件和交付清单 |
| `04_Assets_Set_A`、`04_Assets_Set_B` | 两组原始素材及来源文档 |
| `05_Launch_Scripts` | 游戏和演示入口 |
| `06_Controller_Dependencies` | 控制器依赖和许可材料 |
| `docs` | 构建、运行和实测数据说明 |

## 使用前提

- Ti60F225I3／Ti60F225开发板及匹配显示、存储和输入设备。
- Efinity与RISC-V IDE 2026.1、MinGW make、Python 3、pyserial；素材再生成需要 Pillow。
- 脚本默认工具路径为 `D:/Efinity/` 和 `D:/mingw64/`，可按本机环境调整。

先阅读 `docs/BUILD.md` 和 `docs/RUNTIME.md`。没有完成版本与工具链核对前，不要直接烧录板卡或Flash。

## 主要入口

在完整解压后的仓库根目录运行：

```powershell
& '.\05_Launch_Scripts\启动Garden_C.ps1' -Port COM11
& '.\05_Launch_Scripts\启动基础测试_C.ps1' -Port COM11
& '.\05_Launch_Scripts\启动三Boss2730弹幕_C.ps1' -Port COM11
& '.\05_Launch_Scripts\启动CPU_GPU六画面_C.ps1' -Port COM11
& '.\05_Launch_Scripts\启动CPU_GPU六画面实时对比_C.ps1' -Port COM11
```

正常游戏使用 `cclassic_1102`，基础同屏测试使用 `cbasic_min_1103`，三Boss 2730 演示使用 `cboss_hud_1103`，六画面展示使用 `csix_1103` 或 `csixlive_1103`。HP10 Flash 材料位于 `03_Software_Firmware/garden_hp10_autoboot_20261102`。

## 验证边界

文件哈希只证明交付内容一致，不等同于重新上板或通过最新实板验收。修改源码后必须重新构建并重新确认版本，旧 ELF 不会自动覆盖新源码。
