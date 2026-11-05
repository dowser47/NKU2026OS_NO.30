# Lab3：中断与异常

当前阶段：**P1 已实现并通过验证；P2、P3 尚未实现**。按要求每节结束暂停，所有修改未 commit、未 push。

## 目录

- `code/`：从本仓库最新 lab2 复制的源码、构建文件和工具，新增 Trap/时钟框架。
- `提示词.md`：用户提供的 P1/P2/P3 原始 Prompt 与 Specification。
- `操作说明.md`：编译、验证和答辩演示步骤。
- `知识点梳理.md`：答辩准备，随阶段补充。
- `report/report.md`：按指导书要求撰写的阶段报告，最终版本待 P3 与综合验证完成。
- `code/evidence/p1/`：编译、QEMU/GDB 实测记录与指导书相关正文快照。

## 基础与框架来源

2026-11-05 执行 `git fetch origin`、`git pull --ff-only origin main`，结果为 Already up to date；基线为 `328fd3afd8036fb87a6790b3ee697a05e2100a46`。本地原有 101 个 lab2 验证文件的未提交修改保留。

lab2 源码不含 `kern/trap/`、`clock.c`、`intr.c`，因此不能只补两个汇编空白就完成构建。本次复制 lab2 的 `kern/`、`libs/`、`tools/`、Makefile 与忽略规则，没有复制旧证据或编译产物。lab2 原文件未修改。

缺失的 SAVE_ALL、frame 布局、idt_init、trap_dispatch/trap、clock.c、intr.c 依据本地 `lab2026-book/lab3/` 正文补齐；其余声明及诊断输出补充为可编译框架。这里不是取得了完整的官方 lab3 源码包：旧站尝试的源码路径返回 404，新站 `oslab.mobisys.top` 返回 502。`print_trapframe` 是本地补充的诊断实现。指导书只给出 `SHUTDOWN_AFTER` 名称而未给出数值，本地框架暂取 **10 行**，P2 实施前应明确沿用此设定；若后续取得原始框架，应核对宏值和输出格式。

P1 在新建框架中只实现 `RESTORE_ALL` 与 `__alltraps` 调用序列，署名 `lab3: 2414067`。P2/P3 两个 handler 保留空实现。启动流程完成 lab2 自测后调用 `idt_init()`；时钟初始化及全局中断使能留到 P2，避免空 handler 导致持续重入。

## 当前验证

```bash
cd /home/dowser47/OS/NKU2026OS_NO.30/lab3/code
make -j4
make grade
```

预期：`P1 PASS: real 32-bit ebreak and 16-bit c.ebreak; frame, GPRs, CSRs, sp, sret`。

这是自行编写的 P1 验证，**不是课程官方评分，也不是整个 lab3 完成的证明**。继承的 `grade-boot`、`grade-all`、`grade-integration`、`grade-source` 等工具属于 lab2，不作为 lab3 验收入口。
