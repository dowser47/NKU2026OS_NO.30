# NKU2026OS_NO.30

南开大学 2026 操作系统实验小组仓库。仓库根目录对应本地 `OSLab/`，每个实验放在独立子目录中。

## 实验目录

| 实验 | 说明 | 报告 |
| --- | --- | --- |
| [lab1](lab1/) | lab0 环境配置与 lab1 最小可执行内核、启动流程 | [lab1 实验报告](lab1/report/report.md) |
| [lab2](lab2/) | 预留目录，尚未开始 | — |

运行与调试步骤见 [lab1 使用说明](lab1/code/README.md)，原始验证记录见 [lab1/code/evidence](lab1/code/evidence/)。

```text
root/
├── README.md
├── lab1/
│   ├── code/
│   └── report/
└── lab2/
    └── .gitkeep
```

`lab2/.gitkeep` 是保留空目录的占位文件。

## 编译与验证

在已安装 RISC-V 工具链、QEMU 和 GDB 的 Linux 环境中，从仓库根目录执行：

```bash
cd lab1/code
make
make grade
```

当前电脑也可在 PowerShell 中执行：

```powershell
wsl -d Ubuntu --cd "E:\桌面\OSLab\lab1\code" --exec make grade
```

## 版本管理

源码、实验报告和验证记录上传到 [GitHub](https://github.com/kighter123456/NKU2026OS_NO.30)。离线指导书 `lab2026-book/`、`lab2026-book.zip`、编译产物和 Python 缓存仅保留在本地，由忽略规则排除。
