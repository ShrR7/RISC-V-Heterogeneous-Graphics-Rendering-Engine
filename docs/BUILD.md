# 构建说明

## 工具链

- Efinity与RISC-V IDE 2026.1
- MinGW make
- Python 3、pyserial
- Pillow（仅重新生成素材时需要）

脚本默认 `D:/Efinity/` 与 `D:/mingw64/`。构建前先检查源码、工具版本和目标板型号，禁止直接写入不匹配的硬件。

## 推荐顺序

1. 检查 `03_Software_Firmware/organizer_submission_20261103/文件清单.json` 中的版本与哈希。
2. 编译 FPGA 配置，检查资源、时序和接口约束。
3. 编译对应游戏或Demo的CPU程序，确认ELF与素材一致。
4. 运行主机侧静态检查与可用的仿真。
5. 核对新的bit/ELF/素材组合后，再决定是否进行RAM加载或Flash写入。

各variant使用 `tools/build_fx_variant.ps1` 和 `Makefile.fx`。修改源码后需要重新生成对应运行输入清单，不能继续沿用旧版验收结论。

Windows下建议将仓库放在短路径，或使用支持长路径的工具。
