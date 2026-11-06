# NKU2026OS_NO.30

南开大学 2026 操作系统实验小组仓库。每个实验的代码、报告与验证记录分别放在独立子目录中。本机仓库路径为 `/home/dowser47/OS/NKU2026OS_NO.30`。

## 实验目录

| 实验 | 说明 | 报告 |
| --- | --- | --- |
| [lab1](lab1/) | lab0 环境配置与 lab1 最小可执行内核、启动流程 | [lab1 实验报告](lab1/report/report.md) |
| [lab2](lab2/) | Sv39 启动分页、First/Best Fit/Buddy 页分配器与简化 SLUB | [lab2 实验报告](lab2/report/report.md) |
| [lab3](lab3/) | 基于提供的原始 lab3 框架完成 Trap、时钟与异常；原始评分 100/100 | [lab3 实验报告](lab3/report/report.md) |

运行与调试步骤见 [lab1 使用说明](lab1/code/README.md)、[lab2 使用说明](lab2/code/README.md) 和 [lab3 使用说明](lab3/README.md)。各实验的验收范围与限制以对应报告为准。

```text
root/
├── README.md
├── lab1/
│   ├── code/
│   └── report/
├── lab2/
│   ├── code/
│   └── report/
└── lab3/
    ├── code/
    │   └── evidence/
    └── report/
```

## 编译与验证

需要 RISC-V GCC 工具链、QEMU、Python 3；手动及补充调试另需 RISC-V GDB 或 `gdb-multiarch`。从仓库根目录执行 lab3 的完整验证：

```bash
cd lab3/code
make -j4
make grade
```

当前电脑也可在 PowerShell 中执行：

```powershell
wsl -d Ubuntu --cd /home/dowser47/OS/NKU2026OS_NO.30/lab3/code --exec make grade
```

lab3 已按 `OS/lab3_origin/lab3_new` 重新整理基础代码，补齐 `sync.h`、PMM 中断保护、lab2 留空部分和 Trap 实现。`make grade` 运行原始注入器和课程测试，当前结果为 **Total Score: 100/100**；评分脚本仅适配本机 QEMU 的内核启动参数，未修改断言或分值。新记录见 [lab3 基线整合验证](lab3/code/evidence/baseline-integration/)。

在 `lab3/code` 中执行 `make qemu`：原框架默认 First-Fit 检查通过后，每100次时钟中断输出 `100 ticks`，累计10行后自动关机。异常由评分或补充验证触发。可选执行 `python3 tools/verify_integration.py` 和 `python3 tools/verify_integration.py --best-fit`，检查四种真实16/32位异常、完整GPR恢复和PMM临界区状态；两种配置均通过。

lab1、lab2 的入口见各自说明。lab3 旧版独有的 Buddy/SLUB 和旧验证工具已归档，不混入原始 lab3 框架；lab2 本身保留。历史 P1/P2/P3 证据继续保存，当前使用的产物路径为 `lab3/code/bin/kernel`、`lab3/code/bin/ucore.img`。

## 版本管理与本地资料

源码、实验报告和验证记录由本 [GitHub 仓库](https://github.com/kighter123456/NKU2026OS_NO.30) 管理。离线指导书 `lab2026-book/`、`lab2026-book.zip`、编译产物和 Python 缓存由忽略规则排除。

lab3 的 `操作说明.md`、`提示词.md`、`知识点梳理.md` 仅保存在仓库外的 `OS/lab3/`，不提交到本仓库。仓库内的 lab3 报告独立包含 Prompt/Specification 设计、实现说明、实际问题与验证结果。
