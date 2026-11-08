# 操作系统实验报告

## 实验基本信息

| 项目 | 内容 |
|------|------|
| **实验名称** | Lab 3：中断与异常（断，都可以断） |
| **小组成员** | 2414067－窦思微、2411520－谢国辉、2412267－高家铭 |
| **完成日期** | 2026-11-06 |

### 小组分工

练习按底层通路、异步中断、同步异常依次分工，三个部分共享给定的 Trap frame 和统一返回流程。指导书将后两部分归为“中断与异常处理功能”，组内分别由两名成员负责。

| 成员 | 负责的练习/模块 | 验证重点 |
|------|----------------|----------|
| 2414067－窦思微 | 功能模块一：`trapentry.S` 的 `RESTORE_ALL` 与 `__alltraps` 调用序列，对应提示词 P1 | frame 槽位、CSR 恢复、sp 最后恢复、a0 参数、sret 返回与 GPR 一致性 |
| 2411520－谢国辉 | 功能模块二的中断部分：`trap.c` 的 `interrupt_handler()`，对应提示词 P2 | 下一次事件设置、ticks 计数、每百次输出、累计行数关机及其他中断输出 |
| 2412267－高家铭 | 功能模块二的异常部分：`trap.c` 的 `exception_handler()`，对应提示词 P3；报告汇总 | 非法指令与断点识别、16/32 位指令长度、epc 推进及未知异常兜底 |

实验报告按对应模块撰写和汇总：窦思微负责 frame 布局、恢复顺序和入口不变量；谢国辉负责时钟中断链路及统计、关机逻辑；高家铭负责异常处理小节、整体逻辑、AI 协作问题汇总和最终报告整理。原始框架核对、补齐 lab2 空白和综合验证作为项目整合内容统一记录。

---

## 一、实验目的

1. 理解 RISC-V Trap 的产生、状态保存、统一分发和返回机制，建立“进入→保存→C 处理→恢复→返回”的完整通路。
2. 掌握汇编与 C 通过固定布局的 `struct trapframe` 共享现场的方法，理解 sp、CSR 和普通通用寄存器在保存与恢复时的区别。
3. 实现持续时钟中断、计数输出与关机，并按实际指令长度处理非法指令和断点异常。
4. 理解本地中断屏蔽对物理内存管理临界区的保护作用，区分原状态恢复和无条件开中断。
5. 学习用明确的 Specification 和 Prompt 约束 AI 修改范围，并通过原始评分、QEMU/GDB 和边界验证检查生成结果。

---

## 二、实验环境

| 成员 | AI 编程工具 | 底层模型 | 备注 |
|------|------------|---------|------|
| 2414067－窦思微 | Codex 桌面应用 | GPT-6 ||
| 2411520－谢国辉 | Codex 桌面应用 | GPT-6 ||
| 2412267－高家铭 | Codex 桌面应用 | GPT-6 ||

---

## 三、实验整体逻辑分析

### 3.1 本章节的逻辑主线

本章围绕“发生 Trap 后怎样正确回到原执行流”展开。硬件先记录 `sepc/scause/stval` 并更新 `sstatus`，随后跳至 `stvec`；硬件不会自动把所有 GPR 保存到内存。汇编入口在当前内核栈建立 frame，按 ABI 将其传给 C 函数；C 区分中断和异常，完成对应处理；汇编读取处理后的 frame 恢复寄存器并执行 `sret`。

时钟中断是异步事件，处理后保留原 epc；非法指令和断点是同步异常，本实验要求报告后跳过对应指令。两者共享底层保存和返回机制，但返回地址更新规则不同，因此不能把所有 Trap 都按“epc 加 4”处理。

### 3.2 功能的逐步实现

1. **补齐内存与启动基础。** 在原始 lab3 的 lab2 标记位置补上 `page_init()` 和 Best-Fit，确认内核、Page 数组和空闲页范围正确；保留原框架的 PMM 中断保护，为开中断后的执行提供基础。
2. **完成上下文恢复与入口。** 先保证 frame 布局、sp 恢复和 C 调用接口正确，否则 handler 即使输出正确也可能无法返回。
3. **加入时钟中断。** 在统一通路上重设下一次事件、计数和打印，以持续中断检验保存、处理和恢复能够反复执行。
4. **加入同步异常处理。** 在已可信的返回流程上，根据故障指令长度修改 frame.epc，避免返回后再次执行同一异常指令。
5. **核对原始基线并综合验证。** 检查接口及输出契约，通过原始评分，再以真实异常、完整现场比较和 PMM 状态检查补足边界。

---

## 四、实验内容与实现

### 功能模块一：Trap 上下文恢复与 Trap 入口

**负责人：** 2414067－窦思微

#### 模块功能描述

**需要实现/修改的函数或汇编入口：**

```asm
.macro RESTORE_ALL
.endm

__alltraps:
    # SAVE_ALL 后将 frame 交给 C 入口，返回至 __trapret
```

```c
void trap(struct trapframe *tf);  // 已有接口，保持签名与行为
```

本模块接收 `SAVE_ALL` 建立的 frame，负责将其作为 C 首参数传给 `trap()`，并在 C 返回后恢复处理器现场。frame 的 0～31 号槽对应 x0～x31，32～35 号槽对应 status、epc、badvaddr、cause；RV64 每槽 8 字节，共 288 字节。

`SAVE_ALL` 在分配栈空间前把旧 sp 保存到 sscratch，因此 2 号槽应存陷入前的 sp。`RESTORE_ALL` 先借 s0/s1 写回 sstatus/sepc，再恢复 x1、x3～x31，最后加载 x2。恢复 sp 之后不能再以它为基址访问旧 frame，也不必再额外加回 frame 大小。

`__alltraps` 执行 `mv a0, sp; call trap`。原始 ra 和 a0 已在 frame 中保存，C 调用产生的寄存器变化不会覆盖原现场；返回后进入 `__trapret`，重新从 frame 读取可能被 handler 修改的 epc，最终由 sret 处理返回权限和中断状态。

#### 最终提示词

````markdown
PROMPT

修改 `kern/trap/trapentry.S`：补齐 `RESTORE_ALL` 宏，并在 `__alltraps` 中 `SAVE_ALL` 之后、`__trapret` 之前标有 `lab3: YOUR CODE` 的位置，补上“把当前 Trap frame 交给 `trap()`、并在其返回后进入 `__trapret`”的调用序列，必须直接修改项目中的实际文件而不是只给出代码。不要改动 `SAVE_ALL`、`struct trapframe`、`__trapret` 和 `sret` 的位置与内容，把 `YOUR CODE` 替换为学号并保留署名注释 `lab3: 2414067`，也不要顺带重构其它函数。

SPECIFICATION

`RESTORE_ALL` 必须与已给出的 `SAVE_ALL` 严格对称：frame 的 0~31 号槽位是 x0~x31，32~35 号槽位是 `sstatus`、`sepc`、`stval`、`scause`，而 2 号槽位保存的是陷阱发生前的 sp（由 `sscratch` 交换得到），不是 `sp-36*REGBYTES`。恢复时先把 `sstatus`、`sepc` 写回 CSR，再恢复 x1 与 x3~x31，最后才从 2 号槽位恢复 sp，所有以 sp 为基址的加载都必须在覆盖 sp 之前完成，否则后续加载会读到 frame 之外；x0 不需要恢复，宏内不得再分配栈空间、不得出现 `sret` 或 `ret`、不得手工修改 `sstatus` 的 SIE 与 SPP，宽度和偏移只能用 `REGBYTES` 与 `LOAD/STORE` 表达，不能写死 `ld/sd` 或 288。`__alltraps` 处只需把当前 sp 作为第一个参数（a0）用 `call` 跳转到 `trap()`，调用前后 sp 不变，返回后直接落入 `__trapret`，这样 `trap()` 对 `tf->epc` 的修改才能被再次从 frame 读回。
````

#### 实现迭代过程

本模块按“初始实现与调试、原始基线复核”两个阶段整理。现有记录不能精确还原每一次模型生成，因此不虚构生成轮数。

##### 第一次迭代：完成恢复与入口，修正调试观察方式

**遇到的问题：** GDB 曾在软件单步跨越 `sret` 时停滞，无法可靠观察返回目的位置。该现象不能直接认定为前面的寄存器加载错误。

**问题解决策略：** 核对恢复指令和 frame 后，在保存的 epc 目标设置硬件断点，让 sret 实际执行，再比较返回 PC、sp 和 GPR。验证提示中明确“观察真实返回结果”，避免由调试器代替返回流程。

**阶段结果：** 汇编恢复和入口通过自建验证；该结果仅证明当时被覆盖的恢复契约，不单独证明时钟和异常语义。

##### 第二次迭代：与原始框架复核

**遇到的问题：** 初版框架由指导书重建，需要确认 SAVE_ALL、结构体和固定接口与原始包一致。

**问题解决策略：** 在原始包上合入 RESTORE_ALL 和入口，保留详细中文解释；核对 SAVE_ALL 指令序列和 frame 布局不变。补充真实异常测试时，由 C handler 自行推进 epc，调试器只注入指令和观察。

**最终结果：** 编译通过，原始上下文/断点项 35/35；补充的四种真实异常均返回成功，全部 GPR 与保存值一致。

**关键改进点：** 把“sp 最后恢复”“恢复读取处理后的 epc”“sret 目的地址观察”写成可检查的不变量，而非只要求输出成功提示。

---

### 功能模块二：中断与异常处理

指导书将两个 handler 归为同一模块；组内分别实现和验证，以下按职责展开。

#### 中断处理部分

**负责人：** 2411520－谢国辉

#### 模块功能描述

**需要实现的函数：**

```c
void interrupt_handler(struct trapframe *tf);
```

依赖已有 `clock_set_next_event()`、`print_ticks()`、`ticks`、SBI 关机接口和 frame 诊断函数。S 态时钟中断分支依次预约下一次事件、ticks 加一、逢 100 输出一行；printed_lines 记录实际输出行数，达到 10 行后关机。shutdown_requested 在调用前置位，避免关机接口意外返回后重复请求。

`(tf->cause << 1) >> 1` 使用无符号移位去除最高中断标志。例如 `0x8000000000000005 → 0xa → 0x5`，最后得到原因码 5。最高位在左移时已经丢弃，右移不会找回它。frame 内的原始 cause 不变。

其他 13 种已定义中断只报告类型和原因；未知原因先输出原因码，再用原框架 `print_trapframe(tf)` 兜底。handler 不改 epc、不自行开中断，也不承担真实外设的确认或清除工作。原始包和指导书没有给出 SHUTDOWN_AFTER 的数值，本实现约定 10 行，即正常计数 1000 次中断后关机。

#### 最终提示词

````markdown
PROMPT

请修改 `kern/trap/trap.c`，只实现 `interrupt_handler()`，`exception_handler()` 保持框架中的空实现，必须直接修改项目中的实际文件。不得修改 `trap()`、`trap_dispatch()`、`print_trapframe()`、`idt_init()`、`print_ticks()` 的签名与行为，不得修改 `struct trapframe` 和 `kern/driver/clock.c`、`libs/sbi.h`，可以新增模块内的 static 计数器或辅助函数，署名注释使用 `lab3: 2411520`。

SPECIFICATION

`interrupt_handler()` 处理 `IRQ_S_TIMER` 时先调用 `clock_set_next_event()` 排定下一次时钟事件，再让 `ticks` 加一，当 `ticks` 是 `TICK_NUM` 的整数倍时复用 `print_ticks()` 输出一行统计，累计输出达到 `SHUTDOWN_AFTER` 行后调用框架关机接口，顺序必须是先输出再关机且只关机一次，输出行数要用独立计数器而不是直接拿 `ticks` 比较；其他已定义中断只打印类型与原因码，不计数、不排事件、不关机；无法识别的原因调用 `print_trapframe(tf)` 兜底后正常返回。不得改动 `tf->status`、`tf->cause` 等字段，不得在 handler 内开中断或调用阻塞函数，并且必须正常返回到统一的 Trap 返回流程。

原始基线整合补充：以提供的 lab3_origin/lab3_new 为基准，保留原有 print_regs、trap_in_kernel 及固定分发函数；未知中断先输出 `Unknown interrupt: cause %lu`（使用去除中断标志后的原因码），再调用 print_trapframe(tf)。原始包未定义 SHUTDOWN_AFTER 数值，本项目约定为 10 行。需要的 SBI 声明通过已有头文件引入，不重写原有 SBI 实现。
````

#### 实现迭代过程

##### 第一次迭代：完成时钟行为

**遇到的问题：** 初版继承的 SBI 头文件声明了关机函数，但源文件没有对应定义；只增加 handler 调用不足以完成链接。同时尚未取得原始评分程序。

**问题解决策略：** 初版补齐关机接口并进行行为和 QEMU 验证；提示词明确重设事件、独立行计数和关机前置标志等要求。

**阶段结果：** 自建测试中周期事件和关机可运行。该结论没有覆盖原始框架一致性及所有中断保护路径。

##### 第二次迭代：使用原始框架及其输出契约

**遇到的问题：** 原始包本身已有 SBI 实现；旧框架遗漏 `print_regs`、`trap_in_kernel`，未知中断输出也未包含评分要求的前缀。

**问题解决策略：** 采用原始 SBI 和固定 Trap 函数，只在 handler 空白中合入逻辑；提示词补充 `Unknown interrupt: cause ...` 的格式，随后调用原 frame 诊断。

**最终结果：** 原始持续时钟项 40/40；软件中断和未知中断输出通过评分；正常启动输出 10 行 `100 ticks` 后 QEMU 退出 0。补充测试在 ticks=1000 观察到实际关机调用。

---

#### 异常处理部分

**负责人：** 2412267－高家铭

#### 模块功能描述

**需要实现的函数：**

```c
void exception_handler(struct trapframe *tf);
```

非法指令和断点先输出异常类型及原 epc，再读取指令前 16 位：低两位不等于 `11` 时加 2，等于 `11` 时按实验的 16/32 位范围加 4。只改变 frame.epc，随后由统一恢复宏写回 sepc。直接写硬件 sepc 可能被 frame 中的旧值覆盖，因此不采用该方式。

仅取半字可以处理指令地址只按 2 字节对齐的情况。其他原因只输出 frame，不读取 epc 指向内存，也不改变现场。未处理异常若原址返回且原因持续，可能再次陷入；本实验不提供通用缺页恢复、不可读故障指令的安全取指或长于 32 位编码的完整解码。

#### 最终提示词

````markdown
PROMPT

请修改 `kern/trap/trap.c`，只实现 `exception_handler()`，不要改动已完成的 `interrupt_handler()`，必须直接修改项目中的实际文件。不得修改 `trap()`、`trap_dispatch()`、`print_trapframe()`、`idt_init()`、`print_ticks()` 的签名与行为，不得修改 `struct trapframe` 和 `kern/driver/clock.c`、`libs/sbi.h`，可以新增模块内的 static 辅助函数，署名注释使用 `lab3: 2412267`。

SPECIFICATION

`exception_handler()` 处理非法指令与断点时输出异常类型和 `tf->epc`，并按实际指令长度前移 `tf->epc`：编码低两位不等于 `0b11` 是 16 位压缩指令、加 2，等于 `0b11` 是 32 位指令、加 4，不能对所有异常统一加 4，也不能用循环“重试到不再异常”；其他异常只调用 `print_trapframe(tf)` 兜底，不改 `tf->epc`、不 panic、不关机。只允许修改 frame 中的 `epc`，不得改动 `tf->status`、`tf->cause` 等字段，不得在 handler 内开中断或调用阻塞函数，并且必须正常返回到统一的 Trap 返回流程。

原始基线整合补充：异常输出使用 `Illegal instruction at 0x%016lx` 或 `Breakpoint at 0x%016lx`，输出的是推进前的原始 epc。先读指令前 16 位，再按低两位判断本实验范围内的 2/4 字节长度；其他原因不读取 epc 指向的内存。保留原框架 print_trapframe 的行为与格式。
````

#### 实现迭代过程

##### 第一次迭代：实现按长度推进 epc

**遇到的问题：** 没有观察到 handler 的初次编译或长度逻辑失败；但旧 P1 测试曾在 C 处理前由 GDB 修改 epc，完整 handler 接入后会再次推进，造成测试前提冲突。

**问题解决策略：** 把隔离恢复契约的编辑移到 C 返回之后；对异常语义另写真实异常测试，由 handler 自己计算 epc。最终提示词继续限制只修改 frame.epc，不通过改变权限或重复重试回避错误。

**阶段结果：** 四种 16/32 位异常及边界行为通过自建验证。不能把测试环境调整写成已经发生的生产 handler bug。

##### 第二次迭代：对齐原始异常输出

**遇到的问题：** 原先的两行 `Exception type/Exception epc` 虽包含类型和地址，但与原始评分需要的 `Illegal instruction at`、`Breakpoint at` 不一致。

**问题解决策略：** 在最终提示词中固定输出契约；保留原框架诊断和函数签名，继续按实际编码计算返回位置。

**最终结果：** 原始非法指令与兜底项 25/25；真实非法指令和断点各覆盖 16/32 位，增量为 2/4/2/4，返回后 GPR 恢复一致。额外 8 个长度边界及 15 个其他原因的 frame 不变检查通过。

---

### 基础框架整合：lab2 空白、中断保护与启动修补

**负责人：** 小组共同整合，报告由高家铭汇总

**涉及接口：**

```c
static void page_init(void);
struct Page *alloc_pages(size_t n);
void free_pages(struct Page *base, size_t n);
size_t nr_free_pages(void);
```

`page_init()` 从 DTB 得到内存范围，并限制在原框架承诺的 128 MiB 窗口。在内核 end 之后按页对齐建立 Page 数组，保留固件、内核及描述符页，再把剩余完整页交给分配器。npage 是物理页号上界，不是空闲页数；描述符数组下标需减 nbase。Best-Fit 选择可满足请求的最小块，拆分剩余空间，释放时按地址顺序与前驱、后继合并。给定 basic_check、default_check 和 best_fit_check 未修改。

原始 `sync.h` 保存进入前的 SIE，进入时关闭本地中断，退出时按原状态恢复；alloc/free/nr_free 都在保护内调用后端。嵌套时外层保存 1，内层保存 0，内层退出保持关闭，外层退出才开启。`do { ... } while (0)` 把保存宏包装为一条完整语句，宏体执行一次，可安全放入 if/else；它本身不提供原子性。

**实际问题与修补：**

| 问题 | 原因及处理 | 验证方式 |
|------|------------|----------|
| 初版缺失 PMM 中断保护 | 直接继承 lab2，未引入原始 sync 层；改用原框架实现 | 真实 CSR 检查后端调用期间 SIE 关闭，覆盖开启、关闭、失败分配及嵌套状态 |
| 原始启动命令停在固件，首次评分 0/100 | 本机设备加载器命令下固件 Next Address=0；改用 `-kernel` 交接内核 | 普通启动成功，原始三项评分通过；首次失败日志保留 |
| 开启 satp 后低地址取指衔接 | PC 不会自动加虚实偏移；根项 2 增加恒等映射，再跳转高地址内核 | QEMU 正常进入 C 内核并完成后续测试 |
| 评分注入和日常产物混用 | 评分改在独立临时副本中执行并留存输出 | 当前源码、常规镜像不注入测试，运行入口保持清晰 |

原始 `grade.sh` 只替换 QEMU 启动参数，未修改断言、分值和时限；`inject.py`、`test_lab3.c/.h`、`test_lab3_context.S` 保持原样。固定 Trap 函数沿用原包，`trap.h` 只补注释、不改变接口。没有开展额外 Challenge。

---

## 五、测试与验证

### 5.1 编译和正常运行

以下为本轮基线已执行的命令；验证日期为 2026-11-06。本次报告合并依据已有真实记录，不把历史日志表述为 11 月 8 日重新运行的结果。

```bash
cd /home/dowser47/OS/NKU2026OS_NO.30/lab3/code
make -j4
make qemu
make grade
```

`make qemu` 的实际输出包含：

```text
memory management: default_pmm_manager
physical memory: [0x0000000080000000, 0x0000000088000000)
check_alloc_page() succeeded!
satp virtual address: 0xffffffffc0206000
satp physical address: 0x0000000080206000
++ setup timer interrupts
100 ticks
100 ticks
100 ticks
100 ticks
100 ticks
100 ticks
100 ticks
100 ticks
100 ticks
100 ticks
```

正常运行在 10 行统计后自动关机，QEMU 退出码为 0。普通启动不会主动产生非法指令或断点。时钟事件增量为 100000，本机 time 频率为 10 MHz，目标间隔约 10 ms，但处理开销使 ticks 不等于严格墙钟时间。

### 5.2 原始评分结果

`make grade` 在临时副本中运行原始注入器和课程测试，实际结果节选为：

```text
  - trap entry, register restore, and breakpoint EPC: PASS (35/35)
  - illegal instruction EPC advance and return: PASS (25/25)
  - re-armed supervisor timer and repeated trap return: PASS (40/40)
Total Score: 100/100
```

| 验证项目 | 结果 | 实际记录 |
|------|------|----------|
| 寄存器恢复及断点返回 | 35/35 | [上下文测试串口输出](../code/evidence/baseline-integration/.qemu.GRADE_LAB3_CONTEXT.out) |
| 非法指令、软件中断及未知原因输出 | 25/25 | [异常测试串口输出](../code/evidence/baseline-integration/.qemu.GRADE_LAB3_ILLEGAL.out) |
| 持续时钟，ticks 达到 200 | 40/40 | [时钟测试串口输出](../code/evidence/baseline-integration/.qemu.GRADE_LAB3_TIMER.out) |
| 评分总分 | 100/100 | [完整评分日志](../code/evidence/baseline-integration/grade.log) |
| 常规启动与关机 | 通过，退出 0 | [正常运行日志](../code/evidence/baseline-integration/qemu-normal.log) |

**测试截图与证据说明：** 现有材料提供完整文本日志，没有保存终端截图。

### 5.3 补充边界与真实异常验证

```bash
python3 tools/verify_integration.py
python3 tools/verify_integration.py --best-fit
```

补充程序仅在临时副本中接入测试，分别验证原框架默认 First-Fit 和可选 Best-Fit，不改当前源码或常规镜像。

| 检查 | 方法与结果 |
|------|------------|
| 四种真实硬件异常 | QEMU 执行 16 位非法编码 0x0000、32 位非法写 cycle CSR、c.ebreak、ebreak；cause 为 2/2/3/3，epc 增量为 2/4/2/4 |
| 保存与返回现场 | 比较 trap 入口和 __trapret 的整个 frame，确认 handler 只改 epc；sret 后全部 32 个 GPR 与保存值一致 |
| 指令长度边界 | 两类原因×四种低两位编码，共 8 例；使用仅半字对齐地址，检查 2/4 字节增量 |
| 其他异常 | 15 种原因，epc 置为不可读地址 1，确认不取指、不修改 frame |
| PMM 临界区 | 代理调用真实后端，确认执行期间 SIE 关闭；返回时恢复进入前状态，覆盖失败分配和嵌套保护，空闲页数一致 |
| 异常后继续运行 | 四次异常后恢复启动现场，继续运行到 ticks=1000，10 行统计后 SBI 关机，QEMU 退出 0 |
| Best-Fit 给定检查 | 原始 best_fit_check 通过，6/6 分 |

First-Fit 记录见 [补充验证摘要](../code/evidence/baseline-integration/supplemental/summary.json)，Best-Fit 见 [验证摘要](../code/evidence/baseline-integration/best-fit/summary.json)。整体记录见 [验证总摘要](../code/evidence/baseline-integration/summary.json)，基线整合时的文件对照见 [SHA-256 清单](../code/evidence/baseline-integration/source-comparison.json)。后续仅新增的 sync.h 解释注释不改变执行逻辑，该清单保留为整合时快照。

原始评分在 ticks=200 确认持续时钟，1000 次中断后的关机由普通启动和补充验证另外证明。补充验证不是课程分数的一部分，也不证明用户态切换、多核同步或完整设备驱动已经实现。

---

## 六、实验总结与收获

### 对操作系统的理解

#### 本实验知识点及其与 OS 原理的联系

| 实验机制 | 对应 OS 原理 | 含义、关系与差异 |
|------|-------------|----------------|
| SAVE_ALL / RESTORE_ALL | 上下文保护与恢复 | 保存被打断执行流，当前仍返回同一执行流，没有选择其他任务，不能等同于完整进程切换。 |
| frame 与 ABI | 内核数据布局、软硬件接口 | C 字段顺序与汇编槽位必须对应；x2 是寻址基址且保存值为旧 sp，因此恢复顺序受约束。 |
| CSR 访问 | 特权状态管理 | CSR 有独立编号空间，不能作为 sd/sw 的数据源；需先读到 GPR，再保存到内存。 |
| stvec Direct 与软件分派 | 中断入口和向量组织 | 一个硬件入口后按原因分发，不是每种中断都有独立硬件入口。 |
| sstatus 与 sret | 权限与中断状态恢复 | sret 根据 SPP/SPIE 返回；Trap 可从 S 态进入 S 态，不能强制清 SPP 按用户态返回。 |
| cause 分类与去标志 | 同步异常、异步中断 | 最高位区分两类，其余位表示原因；无符号移位去掉标志不改变 frame 原值。 |
| epc 与指令长度 | 异常恢复与重启 | 时钟不跳过指令；非法指令和断点按实验要求跳过。真实 OS 也可能终止进程、发信号或模拟执行。 |
| 定时器重设与 ticks | 事件驱动与软件计时 | SBI 是单次预约，必须重设；按当前时间加间隔会漂移，计数不是精确墙钟。 |
| SIE 与 STIE | 分层中断屏蔽 | 前者是 S 态全局使能，后者控制 S 定时器来源，含义不同。 |
| PMM 的保存与恢复宏 | 临界区与状态恢复 | 保护当前 hart 的链表操作，不能替代多核锁；volatile 也不等于同步。 |
| Page 数组与 Best-Fit | 物理内存管理和碎片 | 描述符占用物理内存；最佳适应选择最小合适块，但不能保证消除外碎片。 |

#### 重要但本实验未对应的 OS 原理

进程调度与 PCB、系统调用参数检查、用户与内核地址隔离、完整缺页恢复、跨 hart 锁与内存顺序、驱动级中断确认、浮点/向量状态保护、完整用户态双栈切换和嵌套 Trap 栈管理。当前时钟未驱动调度器，未知异常只诊断，没有通用恢复策略。

### AI 协作开发的经验

1. **先确认真实框架，再限定生成范围。** 初版测试通过仍遗漏了原始同步层，说明自建测试不能替代基线核对。最终采用原始包补空白，并保护固定函数和给定测试。
2. **Spec 要写执行顺序和状态不变量。** “sp 最后恢复”“只改变 epc”“先预约后计数”“按进入前状态恢复 SIE”比笼统的“实现中断处理”更容易检查，也能限制无关重构。
3. **输出契约也是接口。** 类型与地址信息完整不代表满足课程评分；取得原始脚本后需核对确切输出，不能通过修改断言绕过失败。
4. **分离实现错误、集成错误与测试前提。** 固件入口为 0 属于启动交接，GDB 单步异常属于观察方式，重复推进 epc 属于旧测试与完整 handler 的冲突，应分别定位。
5. **用真实状态支持结论。** 在 frame 和寄存器上检查副作用，验证真实 SIE 和异常后的继续运行；同时明确单 hart、内核栈及 16/32 位编码范围，避免把通过结果扩大为完整 OS 功能。
