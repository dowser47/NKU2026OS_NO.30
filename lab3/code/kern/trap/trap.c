#include <assert.h>
#include <clock.h>
#include <console.h>
#include <defs.h>
#include <kdebug.h>
#include <memlayout.h>
#include <mmu.h>
#include <riscv.h>
#include <stdio.h>
#include <trap.h>
#include <sbi.h>

#define TICK_NUM 100
/* 原始包与指导书未给出数值：沿用本实验累计输出 10 行后关机的约定。 */
#define SHUTDOWN_AFTER 10

static void print_ticks() {
    cprintf("%d ticks\n", TICK_NUM);
#ifdef DEBUG_GRADE
    cprintf("End of Test.\n");
    panic("EOT: kernel seems ok.");
#endif
}

/* idt_init - initialize IDT to each of the entry points in kern/trap/vectors.S
 */
void idt_init(void) {

    extern void __alltraps(void);
    /* Set sup0 scratch register to 0, indicating to exception vector
       that we are presently executing in the kernel */
    write_csr(sscratch, 0);
    /* Set the exception vector address */
    write_csr(stvec, &__alltraps);
}

/* trap_in_kernel - test if trap happened in kernel */
bool trap_in_kernel(struct trapframe *tf) {
    return (tf->status & SSTATUS_SPP) != 0;
}

void print_trapframe(struct trapframe *tf) {
    cprintf("trapframe at %p\n", tf);
    print_regs(&tf->gpr);
    cprintf("  status   0x%08x\n", tf->status);
    cprintf("  epc      0x%08x\n", tf->epc);
    cprintf("  badvaddr 0x%08x\n", tf->badvaddr);
    cprintf("  cause    0x%08x\n", tf->cause);
}

void print_regs(struct pushregs *gpr) {
    cprintf("  zero     0x%08x\n", gpr->zero);
    cprintf("  ra       0x%08x\n", gpr->ra);
    cprintf("  sp       0x%08x\n", gpr->sp);
    cprintf("  gp       0x%08x\n", gpr->gp);
    cprintf("  tp       0x%08x\n", gpr->tp);
    cprintf("  t0       0x%08x\n", gpr->t0);
    cprintf("  t1       0x%08x\n", gpr->t1);
    cprintf("  t2       0x%08x\n", gpr->t2);
    cprintf("  s0       0x%08x\n", gpr->s0);
    cprintf("  s1       0x%08x\n", gpr->s1);
    cprintf("  a0       0x%08x\n", gpr->a0);
    cprintf("  a1       0x%08x\n", gpr->a1);
    cprintf("  a2       0x%08x\n", gpr->a2);
    cprintf("  a3       0x%08x\n", gpr->a3);
    cprintf("  a4       0x%08x\n", gpr->a4);
    cprintf("  a5       0x%08x\n", gpr->a5);
    cprintf("  a6       0x%08x\n", gpr->a6);
    cprintf("  a7       0x%08x\n", gpr->a7);
    cprintf("  s2       0x%08x\n", gpr->s2);
    cprintf("  s3       0x%08x\n", gpr->s3);
    cprintf("  s4       0x%08x\n", gpr->s4);
    cprintf("  s5       0x%08x\n", gpr->s5);
    cprintf("  s6       0x%08x\n", gpr->s6);
    cprintf("  s7       0x%08x\n", gpr->s7);
    cprintf("  s8       0x%08x\n", gpr->s8);
    cprintf("  s9       0x%08x\n", gpr->s9);
    cprintf("  s10      0x%08x\n", gpr->s10);
    cprintf("  s11      0x%08x\n", gpr->s11);
    cprintf("  t3       0x%08x\n", gpr->t3);
    cprintf("  t4       0x%08x\n", gpr->t4);
    cprintf("  t5       0x%08x\n", gpr->t5);
    cprintf("  t6       0x%08x\n", gpr->t6);
}

void interrupt_handler(struct trapframe *tf) {
    /* lab3: 2411520 */
    /*
     * 静态变量跨中断保留，启动时由 BSS 清零。
     * printed_lines 记录实际统计行数，不能用 ticks 的绝对值替代。
     * shutdown_requested 防止关机接口意外返回后重复请求关机。
     */
    static size_t printed_lines;
    static bool shutdown_requested;
    /*
     * RV64 的 scause/frame.cause 是 64 位：
     *   bit63：中断标志（1=中断，0=同步异常）；bit62～0：具体原因码。
     * 这里需要去掉 bit63，才能和 IRQ_S_TIMER=5 等原因常量进行比较。
     *
     * tf->cause 的 uintptr_t 在本项目中为 uint64_t，因此两次移位都是
     * 64 位无符号运算：左移丢掉超出 bit63 的位，右移在高位补 0。
     * 对原位串 [I c62 c61 ... c1 c0]，I 表示最高的中断标志：
     *   << 1 得 [c62 c61 ... c1 c0 0]，I 已被丢弃；
     *   >> 1 得 [0 c62 c61 ... c1 c0]，其余原因位回到原位置。
     * 第二次移位不能找回已丢掉的 I，所以这不是可逆的“先乘 2 再除 2”。
     *
     * S 态时钟中断举例：
     *   原值 0x8000000000000005（最高位为 1，原因码为 5）
     *   左移 0x000000000000000a（最高位溢出丢弃）
     *   右移 0x0000000000000005（得到用于 switch 的原因码）
     * 以 8 位数便于观察，同样是 10000101 → 00001010 → 00000101。
     * RV64 下等价于 tf->cause & 0x7fffffffffffffff；移位写法不需硬编码掩码。
     * 关键是操作数必须无符号；不能把本表达式改成有符号数移位。
     * 结果保存在局部变量 cause 中，frame 内的 tf->cause 原值不变。
     */
    uintptr_t cause = (tf->cause << 1) >> 1;
    const char *type;

    switch (cause) {
    case IRQ_S_TIMER:
        /* SBI 只预约一次事件，必须先安排下次事件，才能持续产生中断。 */
        clock_set_next_event();
        /* ticks 仅在 S 态时钟中断分支递增；它表示处理次数，不是精确墙钟。 */
        ++ticks;
        if (ticks % TICK_NUM == 0) {
            /* 先输出，再计入实际输出行数，最后判断是否到达关机阈值。 */
            print_ticks();
            ++printed_lines;
            if (printed_lines >= SHUTDOWN_AFTER && !shutdown_requested) {
                /* 先置标志再调用，确保接口返回时也不会再次请求关机。 */
                shutdown_requested = 1;
                sbi_shutdown();
            }
        }
        return;
    /*
     * 其余框架已定义原因只分类报告，不修改 ticks，不预约事件或关机。
     * 这里不承担真实外设驱动的中断确认/清除工作。
     */
    case IRQ_U_SOFT:  type = "User software"; break;
    case IRQ_S_SOFT:  type = "Supervisor software"; break;
    case IRQ_H_SOFT:  type = "Hypervisor software"; break;
    case IRQ_M_SOFT:  type = "Machine software"; break;
    case IRQ_U_TIMER: type = "User timer"; break;
    case IRQ_H_TIMER: type = "Hypervisor timer"; break;
    case IRQ_M_TIMER: type = "Machine timer"; break;
    case IRQ_U_EXT:   type = "User external"; break;
    case IRQ_S_EXT:   type = "Supervisor external"; break;
    case IRQ_H_EXT:   type = "Hypervisor external"; break;
    case IRQ_M_EXT:   type = "Machine external"; break;
    case IRQ_COP:     type = "Coprocessor"; break;
    case IRQ_HOST:    type = "Host"; break;
    default:
        /* 先报告原因码，再按原框架输出现场；不修改被保存的状态。 */
        cprintf("Unknown interrupt: cause %lu\n", (unsigned long)cause);
        print_trapframe(tf);
        return;
    }
    cprintf("%s interrupt (cause=%lu)\n", type, (unsigned long)cause);
}

void exception_handler(struct trapframe *tf) {
    /* lab3: 2412267 */
    /* 同步异常没有最高位的中断标志，直接用 cause 区分原因。 */
    const char *type;
    switch (tf->cause) {
    case CAUSE_ILLEGAL_INSTRUCTION:
        type = "Illegal instruction";
        break;
    case CAUSE_BREAKPOINT:
        type = "Breakpoint";
        break;
    default:
        /*
         * 其他异常只诊断，不假设“跳过一条指令”就能恢复。
         * 此分支也不读取 epc 指向的内存，frame 所有字段保持不变。
         */
        print_trapframe(tf);
        return;
    }

    /* 先打印故障指令的原地址；推进后的 epc 是恢复地址，不是故障位置。 */
    cprintf("%s at 0x%016lx\n", type, (unsigned long)tf->epc);
    /*
     * 仅取指令的前 16 位即可判断低两位，避免要求 epc 四字节对齐。
     * volatile 保证实际读取该地址；这里假设异常指令可由内核读取。
     * 按实验范围：低两位非 11 为 16 位压缩编码，11 为 32 位编码；
     * 本实现不扩展为 48 位及更长指令的完整长度解码器。
     */
    uint16_t instruction = *(const volatile uint16_t *)tf->epc;
    /* 只修改 frame 的 epc；RESTORE_ALL 会把它写回 sepc，避免重复陷入。 */
    tf->epc += (instruction & 3) == 3 ? 4 : 2;
}


static inline void trap_dispatch(struct trapframe *tf) {
    if ((intptr_t)tf->cause < 0) {
        // interrupts
        interrupt_handler(tf);
    } else {
        // exceptions
        exception_handler(tf);
    }
}

/* *
 * trap - handles or dispatches an exception/interrupt. if and when trap()
 * returns,
 * the code in kern/trap/trapentry.S restores the old CPU state saved in the
 * trapframe and then uses the iret instruction to return from the exception.
 * */
void trap(struct trapframe *tf) {
    // dispatch based on what type of trap occurred
    trap_dispatch(tf);
}
