# Lab3 实验报告：中断与异常

2026-11-06，基于提供的 `lab3_origin/lab3_new` 完成基线整合。原始评分测试100/100；补充QEMU/GDB验证在First-Fit和Best-Fit下均通过。本次未执行commit或push。

## 1. 实验目标与整体逻辑

本实验在lab2物理内存管理基础上建立完整Trap通路：硬件记录CSR并跳至stvec，汇编保存上下文并按ABI调用C入口，C按原因处理中断或异常，汇编恢复现场，最后sret返回。先保证上下文与调用边界可信，再实现时钟事件，最后处理需要推进返回位置的同步异常。

时钟中断应返回原执行位置；非法指令和断点按题目要求跳过故障指令。两者共享入口和恢复代码，但epc语义不同。只打印正确提示不能证明实验成功，还要检查寄存器、栈和异常之后的执行流。

## 2. 基线、目录与补齐范围

最初从lab2复制并依据离线指导书重建框架，遗漏了原始sync.h、PMM临界区和若干诊断接口。此前自建测试通过只说明当时覆盖的执行路径正常，不能证明与题目原始代码一致。本轮取得用户提供的原始包后，以它替换kern、libs、tools和Makefile，再填入题目空白。

56个原始文件全部覆盖，46个逐字节一致；修改清单和两侧SHA-256见 `../code/evidence/baseline-integration/source-comparison.json`。旧版lab3完整归档于仓库外 `OS/lab3/backups/20261106-151429/lab3-before-origin.tar.gz`。旧版Buddy/SLUB及lab2专用验证工具不再混入当前lab3，lab2原工程保留。旧P1/P2/P3日志只作为历史记录。

原始包在pmm.c留下page_init空白，在best_fit_pmm.c留下算法与注册空白。本轮按lab2的内存管理思路适配原始Page结构：使用DTB得到内存范围，限制在框架128MiB窗口内；在内核end之后按页对齐建立Page数组，保留固件、内核和描述符占用页，再交出剩余完整页。npage是最高物理页号的上界，数组下标需要减nbase。原框架默认First-Fit；通过PMM_USE_BEST_FIT可切换Best-Fit。

Best-Fit遍历地址有序空闲链表，选择满足请求的最小块，精确匹配时提前结束；分配时拆分，释放后与相邻前驱、后继合并。原给定basic_check、default_check、best_fit_check均未修改。

## 3. Trap上下文恢复与入口

### 3.1 Spec和Prompt

Spec要求RESTORE_ALL严格对应给定SAVE_ALL布局：0～31号槽为GPR，32～35为status/epc/badvaddr/cause；先借临时GPR恢复sstatus、sepc，再恢复GPR，sp最后恢复。恢复宏不另行分配栈，不在宏中返回，不强行清SPP。入口把当前frame地址放入a0后call trap，返回到__trapret并sret。

Prompt的关键约束是：“仅补RESTORE_ALL和__alltraps目标位置；保留SAVE_ALL、结构体和trap签名；所有偏移使用REGBYTES及LOAD；x2从2号槽最后恢复；恢复的epc必须重新读取frame；署名lab3: 2414067。”原提示词文件保存在仓库外OS/lab3，报告独立说明其约束。

### 3.2 核心实现

SAVE_ALL在sp减小前用sscratch暂存旧sp。普通GPR先按编号槽位保存，再借已保存的s0～s4读取旧sp与CSR，写入2号及32～35号槽。CSR不能作为sd/sw的数据源，需先用csrr读入GPR，再存到内存。

RESTORE_ALL先用s0/s1读取32/33号槽，写回sstatus/sepc，再加载x1、x3～x31，最后加载x2。最后一步既恢复旧栈顶，也释放当前frame；不能再额外加288。x0恒为0，不需要恢复；scause/stval是诊断状态，不是sret需要的返回输入。

__alltraps执行SAVE_ALL后，a0=sp并call trap。C调用可破坏当前caller-saved寄存器，因为原值已保存在frame里。C返回后直接落入__trapret，RESTORE_ALL读取handler修改后的epc，再由sret完成PC及权限/中断状态转换。

汇编中的.text/.macro/.endm/.globl/.align是汇编器指令，在汇编阶段管理代码段、宏、符号和对齐；mv/call是会展开为机器指令的伪指令。详细中文注释保留在trapentry.S。

## 4. 中断与时钟处理

### 4.1 Spec和Prompt

Spec要求先去掉scause最高中断标志，IRQ_S_TIMER分支每次先重设事件再增加ticks；每100次调用原print_ticks，累计输出达到阈值后请求关机。其他已知原因只报告，未知原因输出原因码和原框架frame。所有分支不得修改保存的frame，不主动开中断。

Prompt关键要求：“仅实现interrupt_handler，保护trap.c已有函数；复用clock_set_next_event、ticks和print_ticks；以实际输出行数计数，先输出后关机，关机标志在调用前设置；保持epc/status/cause不变；署名lab3: 2411520。”

### 4.2 核心实现

uintptr_t为64位无符号类型。对 `[I c62 ... c0]` 左移1位得到 `[c62 ... c0 0]`，最高I被丢弃；再逻辑右移得到 `[0 c62 ... c0]`。因此 `(tf->cause << 1) >> 1` 清掉中断标志，保留原因码。例如 `0x8000000000000005 → 0xa → 0x5`。第二步不能找回已经丢掉的最高位，这不是可逆乘除运算。

S态时钟分支调用clock_set_next_event预约下一事件，ticks自增，逢100输出。printed_lines记录实际输出行数，shutdown_requested在SBI调用前置位，避免接口意外返回后重复请求。原始包和指导书均未定义SHUTDOWN_AFTER数值，本项目沿用10行，即正常连续计数1000次后关机。

其他13种已定义原因只分类报告。未知原因先输出 `Unknown interrupt: cause ...` 再print_trapframe，以匹配原始测试。真实外设的确认、清除操作不属于当前分支功能。

## 5. 非法指令与断点异常

### 5.1 Spec和Prompt

Spec把异常分成两类：非法指令和断点输出原位置并按编码长度推进epc；其他原因仅调用print_trapframe，不取指、不改现场。需要处理的指令取其前16位，低两位非11加2，是11按本实验16/32位范围加4。

Prompt关键要求：“仅实现exception_handler；不得改变interrupt_handler和其他固定函数；仅允许修改frame.epc，禁止直接改sepc、改变权限或开中断；输出故障原地址；未知原因不读取epc；署名lab3: 2412267。”取得原始评分脚本后，将输出进一步约束为 `Illegal instruction at 0x...` 和 `Breakpoint at 0x...`。

### 5.2 核心实现

switch选定两类异常的名称，先输出处理前epc，再以volatile uint16_t读取前缀。半字读取能处理仅2字节对齐的指令地址，无需强制32位访存。handler只修改frame.epc，统一恢复宏再把它写回sepc。直接修改硬件sepc可能被后续恢复旧frame覆盖，因此不采用该做法。

其他原因保持frame不变并诊断。它们原址返回后若条件持续，可能重新陷入；这不构成通用异常恢复。当前未实现48位及更长指令的完整解码器，也未为不可读故障指令实现通用安全取指。

## 6. 中断保护与基础框架修补

原始sync.h的local_intr_save保存进入前SIE，若原本开启则关闭；local_intr_restore只在保存值为真时开启。PMM的alloc_pages、free_pages和nr_free_pages都在这一保护内调用后端，避免同一hart被中断后重入共享链表。

不能简单用intr_disable/无条件intr_enable替代：在Trap或外层临界区内调用时，原状态就是关闭，退出后也应保持关闭。嵌套时外层保存1、内层保存0，先退出内层仍关闭，外层退出才开启。这是本地中断互斥，不是多hart锁；原始实现不改，增加中文解释。

entry.S增加根项2的低地址恒等映射，与根项511的高地址映射指向同一物理区间。satp生效后PC不会自动加虚实偏移，需要低地址映射继续执行刷新与跳转，再进入高地址C内核。

原始Makefile和grade.sh使用设备加载器。本机固件在该命令下报告Next Address=0，未进入内核。替换为-kernel后能正确交接。grade.sh只改这一启动参数；原始inject.py、test_lab3三份文件、评分断言、分数与时限全部保留。Makefile另增加GDB兼容选择、独立临时评分副本和日志保存。

trap.c中已有的print_ticks、idt_init、trap_in_kernel、print_trapframe、print_regs、trap_dispatch和trap逐一核对保持原样；trap.h仅加注释，不改变布局。原框架按%08x显示诊断字段，RV64完整寄存器值可通过GDB查看。

## 7. 实验验证与结果

在code目录执行make -j4、make qemu、make grade。原始评分在临时副本注入测试，不改当前源码或常规镜像。

| 验证项目 | 实际结果 | 证据 |
| --- | --- | --- |
| 原始寄存器恢复与断点返回 | 35/35 | baseline-integration/.qemu.GRADE_LAB3_CONTEXT.out |
| 原始非法指令、软件中断与未知原因输出 | 25/25 | baseline-integration/.qemu.GRADE_LAB3_ILLEGAL.out |
| 原始持续时钟，ticks达到200 | 40/40 | baseline-integration/.qemu.GRADE_LAB3_TIMER.out |
| 原始评分总分 | Total Score: 100/100 | baseline-integration/grade.log |
| 正常内核启动至关机 | First-Fit检查通过，10行100 ticks，QEMU退出0 | baseline-integration/qemu-normal.log |
| 补充First-Fit真实验证 | 全通过 | baseline-integration/supplemental/summary.json |
| 补充Best-Fit真实验证 | 全通过，原Best-Fit检查6/6 | baseline-integration/best-fit/summary.json |
| 与原始包对照 | 无缺失，46文件原样；保护函数与测试核对通过 | baseline-integration/source-comparison.json |

上表路径均相对于 `../code/evidence/`。可选执行 `python3 tools/verify_integration.py` 及加 `--best-fit` 的版本重现补充验证。

补充验证在临时副本中真实运行CSR和分配器，通过代理确认后端调用期间SIE关闭，覆盖进入前开启/关闭、失败分配和嵌套临界区；前后空闲页数一致。对两类异常×四种低位编码进行8次直接调用，检查只改变epc；对15种其他原因将epc设为1，确认不取指也不改frame。

GDB另在运行内存中注入0x0000、非法写只读cycle CSR、c.ebreak、ebreak，四次真实硬件异常原因分别为2/2/3/3，返回增量2/4/2/4。在trap入口与__trapret比较frame，确认仅epc变化，sret后全部32个GPR与保存值一致。调试器不替handler计算epc。最后恢复原启动现场，运行至ticks=1000并实际SBI关机，两种内存配置均退出0。

## 8. AI生成与集成中实际遇到的问题

1. **框架补齐不完整。** 最初把lab2代码直接继承，未引入sync.h与PMM中断保护，且诊断函数和输出格式与原始框架不同。此前内存检查在开中断前执行，时钟也不分配页，因而测试没有暴露缺失。本轮改用提供的原始包，补齐空白，并新增真实SIE状态验证。
2. **原始启动参数不适配本机。** 第一次原始评分0/100，串口只停留在固件，Next Address=0；改为-kernel后全部通过。初次失败日志保存在baseline-integration/initial-loader-failure，不通过修改断言绕过失败。
3. **开启分页后的取指衔接。** 保留此前已经确认必要的恒等映射修复，使当前低地址PC在satp生效后有效，再跳到高地址内核。
4. **GDB软件单步sret曾停滞。** 历史调试改为在sepc目标设置硬件断点，让sret实际执行后检查返回结果；当前补充测试和手动说明沿用该方式。

没有发现本轮Trap恢复和handler在原始评分及补充测试中的新失败；不把测试通过扩大解释为所有OS机制均已实现。

## 9. 知识点与OS原理对应

| 实验机制 | 原理与联系 | 差异或边界 |
| --- | --- | --- |
| SAVE_ALL/RESTORE_ALL | 上下文保护与恢复 | 当前返回同一执行流，不是调度另一个进程。 |
| trapframe及ABI调用 | 内核数据布局、汇编与C接口 | 字段顺序约束固定偏移，sp还受基址寻址限制。 |
| stvec Direct与软件分派 | 中断向量组织 | 单一硬件入口，不是每类原因独立向量。 |
| sstatus/sret | 权限及中断状态转换 | Trap可由S态进入S态，不能强行按用户态返回。 |
| sepc与指令长度 | 同步异常恢复 | 本实验选择报告后跳过；真实OS还可能终止进程或模拟指令。 |
| 定时器重设与ticks | 事件驱动、软件计时 | SBI单次预约；现在+间隔的实现存在开销与漂移，ticks不是严格墙钟。 |
| SIE、STIE | 分层中断屏蔽 | 全局使能与特定来源使能不同，均需满足。 |
| PMM中断保护 | 临界区互斥与状态恢复 | 仅保护本地hart，不能代替多核锁；volatile也不能替代同步。 |
| page_init与空闲链表 | 内存描述、分配及碎片 | Page数组消耗实际物理页；最佳适应不保证消除外碎片。 |

## 10. 重要但本实验未覆盖的原理

进程调度与PCB、系统调用和参数检查、用户/内核地址隔离、完整缺页处理、跨hart锁与内存顺序、驱动级中断确认、浮点/向量状态保护、完整用户态双栈切换和嵌套Trap栈管理。当前时钟没有驱动调度器，未知异常也没有通用恢复策略。

## 11. 实验体会

框架一致性本身属于正确性的一部分。自建测试只能证明覆盖到的行为，不能替代原始代码核对和课程测试。此次同时保留基线对照、原始评分和真实边界验证：既检查输出契约，也检查寄存器、frame和中断状态，才能解释“为什么能正常返回并持续执行”。个人操作、提示词和答辩资料仍位于仓库外OS/lab3。
