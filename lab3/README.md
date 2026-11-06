# Lab3：中断与异常

2026-11-06 已按用户提供的 `OS/lab3_origin/lab3_new` 重新整理基础框架，完成原始代码空白并验证。原始评分测试 **100/100**，补充的真实 QEMU/GDB 验证在 First-Fit、Best-Fit 两种配置下均通过。没有 commit 或 push。

## 目录与来源

- `code/`：以提供的原始 lab3 包为基础的当前工程。
- `report/report.md`：实现、Spec/Prompt、实际问题与验证结果。
- `code/evidence/baseline-integration/`：本轮评分、正常运行、补充验证和源文件对照清单。
- `code/evidence/p1/`、`p2/`、`p3/`：旧版阶段验证的历史记录，不代表本轮基线。

`操作说明.md`、`提示词.md`、`知识点梳理.md` 仅保存在仓库外 `OS/lab3/`。替换前的整个 lab3（含源码、报告、证据）备份在 `OS/lab3/backups/20261106-151429/lab3-before-origin.tar.gz`。

原先从 lab2 复制并按指导书重建框架的版本已归档。本轮以原始包替换 `kern/`、`libs/`、`tools/`、Makefile；旧版独有的 Buddy、SLUB、自测和旧验证工具不混入当前 lab3，它们仍在 lab2 和备份中保留。

## 相对原始包的必要修改

56 个原始文件全部覆盖，46 个内容完全一致；详见 `code/evidence/baseline-integration/source-comparison.json`。

| 文件 | 修改及原因 |
| --- | --- |
| `kern/trap/trapentry.S` | 填写恢复宏和调用入口，保留详细中文注释；SAVE_ALL 指令与原始包一致。 |
| `kern/trap/trap.h` | 仅增加布局注释，不改结构或接口。 |
| `kern/trap/trap.c` | 填写两个 handler，增加 SBI 头文件和 SHUTDOWN_AFTER=10；其他函数沿用原始包。 |
| `kern/mm/pmm.c` | 填写 lab2 的 page_init，保留原始中断保护，增加可选 Best-Fit 编译分支。 |
| `kern/mm/best_fit_pmm.c` | 填写 lab2 算法空白和分配器注册；原给定检查函数原样保留。 |
| `kern/sync/sync.h` | 只增加保存/恢复及嵌套语义的中文注释。 |
| `kern/init/entry.S` | 增加根项 2 的恒等映射，确保开启 satp 后低地址 PC 仍可取指。 |
| `kern/init/init.c` | 仅清理一条说明性注释；启动逻辑沿用原始包。 |
| `Makefile` | QEMU 用 -kernel 启动；GDB 自动兼容 gdb-multiarch；评分在临时副本注入，保存日志。 |
| `tools/grade.sh` | 只将设备加载器启动参数换成 -kernel；测试、分值、断言均不改。 |

新增 `tools/verify_integration.py`、`tools/test_integration.c` 作为补充验证。补充测试只接入临时副本，不进入正常内核。原始 `inject.py`、`test_lab3.c/.h`、`test_lab3_context.S` 均保持原样。

## 编译、运行与评分

```bash
cd /home/dowser47/OS/NKU2026OS_NO.30/lab3/code
make -j4
make qemu
make grade
```

需要 RISC-V GCC、QEMU、Python 3；手动或补充调试还需 RISC-V GDB / gdb-multiarch。当前产物为 `bin/kernel` 和 `bin/ucore.img`。

`make qemu` 使用原框架默认 First-Fit，通过 `check_alloc_page()` 后开启时钟，每100次中断输出一行 `100 ticks`，10行后自动关机。普通启动不会主动产生非法指令或断点。原始包和指导书都未给 SHUTDOWN_AFTER 数值，本实现继续采用10行；100000个 time 单位的间隔在本机10MHz时间基准下约为10ms。

`make grade` 使用原始注入器及评分用例，在临时副本构建三次，最终输出 `Total Score: 100/100`。三项分别为寄存器恢复与断点返回35分、非法指令及兜底输出25分、持续时钟40分。当前源码和常规镜像不会被注入。

评分日志位于 `evidence/baseline-integration/grade.log`；三份串口输出为同目录下 `.qemu.GRADE_LAB3_*.out`。旧的 `grade-p1/grade-p2/grade-p3` 和 lab2 验收入口不再使用。

## 补充验证

```bash
python3 tools/verify_integration.py
python3 tools/verify_integration.py --best-fit
```

两次均在临时副本编译和调试，当前镜像不变。验证真实16/32位非法指令及断点、32个GPR恢复、handler仅改变epc、8种长度边界和15种异常兜底；并通过代理调用原分配器，确认 alloc/free/nr_free 后端执行时SIE关闭，返回时恢复原状态，覆盖失败分配及嵌套临界区。最后运行到ticks=1000并实际关机。Best-Fit还通过原检查函数6/6分。

对应记录位于 `evidence/baseline-integration/supplemental/` 和 `best-fit/`。这两项是补充自建验证，与100分的原始评分分别记录。正式评分脚本在ticks=200确认持续时钟，正常运行和补充测试另外验证1000次中断后的关机。

异常输出使用评分要求的 `Illegal instruction at 0x...`、`Breakpoint at 0x...`；未知中断先输出 `Unknown interrupt: cause ...` 再调用原框架的 print_trapframe。框架原有诊断格式按要求保留。
