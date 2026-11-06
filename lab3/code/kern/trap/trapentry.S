#include <riscv.h>

/*
 * Trap 的汇编包装层：保存现场 → 调用 C 处理函数 → 恢复现场 → sret。
 * 内存布局必须与 trap.h 中的 struct trapframe 一致：
 *   槽位 0～31：x0～x31；32～35：sstatus、sepc、stval、scause。
 * 每槽宽度使用 REGBYTES，LOAD/STORE 随目标架构选择对应的访存指令。
 * 当前实现直接使用被打断的内核栈，不包含用户栈与内核栈的切换。
 *
 * 下面以点号开头的 .text/.macro/.endm/.globl/.align 是汇编器指令，
 * 在汇编期间控制段、宏、符号和对齐，不是运行时由 CPU 执行的操作码：
 *   .text：切换到代码段，后续产生的机器指令放在这里。
 *   .macro 名称 / .endm：定义宏的起止；使用宏名时由汇编器原地展开。
 *     SAVE_ALL/RESTORE_ALL 因此不需要 call，也不会自己建立返回地址。
 *   .globl 符号：把符号声明为全局，供其他目标文件的链接引用使用。
 *   .align 2：本 RISC-V GNU 汇编器按 2^2=4 字节对齐，必要时插入填充。
 * __alltraps: 这种带冒号的标签则为当前位置命名，本身也不执行操作。
 * mv/call 等是另一类“伪指令”：汇编器会把它们转换为可执行指令序列。
 */
    .text
    .macro SAVE_ALL
    /*
     * x2 和 sp 是同一个寄存器的两种名字，不是两个独立寄存器。
     * 假设陷入前 sp=S，RV64 的 frame 大小为 36*8=288 字节：
     *   csrw 后 sscratch=S；addi 后 sp=S-288，为整个 frame 的寻址基址。
     * 因此后面若直接 STORE x2，只能存入 S-288，丢失真正的原始 sp。
     */
    csrw sscratch, sp
    addi sp, sp, -36 * REGBYTES
    /*
     * “按编号”指存储布局：xi 对应 sp+i*REGBYTES，不要求按此时间顺序执行。
     * RISC-V 没有在此自动 push；STORE 是向已分配的 frame 写入固定槽位。
     * x1、x3～x31 此时仍是原值，可直接保存；2 号槽先留空，稍后存旧 sp。
     */
    STORE x0, 0*REGBYTES(sp)
    STORE x1, 1*REGBYTES(sp)
    STORE x3, 3*REGBYTES(sp)
    STORE x4, 4*REGBYTES(sp)
    STORE x5, 5*REGBYTES(sp)
    STORE x6, 6*REGBYTES(sp)
    STORE x7, 7*REGBYTES(sp)
    STORE x8, 8*REGBYTES(sp)
    STORE x9, 9*REGBYTES(sp)
    STORE x10, 10*REGBYTES(sp)
    STORE x11, 11*REGBYTES(sp)
    STORE x12, 12*REGBYTES(sp)
    STORE x13, 13*REGBYTES(sp)
    STORE x14, 14*REGBYTES(sp)
    STORE x15, 15*REGBYTES(sp)
    STORE x16, 16*REGBYTES(sp)
    STORE x17, 17*REGBYTES(sp)
    STORE x18, 18*REGBYTES(sp)
    STORE x19, 19*REGBYTES(sp)
    STORE x20, 20*REGBYTES(sp)
    STORE x21, 21*REGBYTES(sp)
    STORE x22, 22*REGBYTES(sp)
    STORE x23, 23*REGBYTES(sp)
    STORE x24, 24*REGBYTES(sp)
    STORE x25, 25*REGBYTES(sp)
    STORE x26, 26*REGBYTES(sp)
    STORE x27, 27*REGBYTES(sp)
    STORE x28, 28*REGBYTES(sp)
    STORE x29, 29*REGBYTES(sp)
    STORE x30, 30*REGBYTES(sp)
    STORE x31, 31*REGBYTES(sp)
    /*
     * s0～s4 的原值已经入栈，现在可以借作临时寄存器。
     * csrrw 同时取回旧 sp 并把 sscratch 清零；CSR 不能直接存入内存，
     * 因此先读到通用寄存器，再写入相应的 frame 槽位。
     * 原因是 ISA 的操作数限制：sd/sw 的数据源只能是通用寄存器 x0～x31，
     * CSR 有独立的编号空间，不能当作 store 的数据源，也不是普通内存地址。
     * 例如保存 sstatus 必须分两步：csrr s1,sstatus；STORE s1,32*REGBYTES(sp)。
     * csrr 是 CSR 读取伪指令，实际可展开为 csrrs rd,csr,x0。
     * s0 原值已保存在 8 号槽，故现在覆盖 s0 取旧 sp 是安全的；
     * 随后的 STORE s0,2*REGBYTES(sp) 最终把 S 写入 2 号槽。
     */
    csrrw s0, sscratch, x0
    csrr s1, sstatus
    csrr s2, sepc
    csrr s3, sbadaddr
    /* sbadaddr 是本工具链接受的旧名称，对应现在的 stval。 */
    csrr s4, scause
    STORE s0, 2*REGBYTES(sp)
    STORE s1, 32*REGBYTES(sp)
    STORE s2, 33*REGBYTES(sp)
    STORE s3, 34*REGBYTES(sp)
    STORE s4, 35*REGBYTES(sp)
    .endm

    .macro RESTORE_ALL
    /* lab3: 2414067
     * 从 frame 重新读取返回状态，保留 C handler 对 epc 的修改。
     * 先借 s0/s1 写回 CSR，再恢复它们的原值，避免临时值覆盖原现场。
     * 不手工改 SIE/SPP；返回权限与中断状态交给 sret 按保存值处理。
     */
    LOAD s0, 32*REGBYTES(sp)
    LOAD s1, 33*REGBYTES(sp)
    csrw sstatus, s0
    csrw sepc, s1
    /* x0 恒为零，无须恢复；x2 作为所有加载的基址，必须留到最后。 */
    LOAD x1, 1*REGBYTES(sp)
    LOAD x3, 3*REGBYTES(sp)
    LOAD x4, 4*REGBYTES(sp)
    LOAD x5, 5*REGBYTES(sp)
    LOAD x6, 6*REGBYTES(sp)
    LOAD x7, 7*REGBYTES(sp)
    LOAD x8, 8*REGBYTES(sp)
    LOAD x9, 9*REGBYTES(sp)
    LOAD x10, 10*REGBYTES(sp)
    LOAD x11, 11*REGBYTES(sp)
    LOAD x12, 12*REGBYTES(sp)
    LOAD x13, 13*REGBYTES(sp)
    LOAD x14, 14*REGBYTES(sp)
    LOAD x15, 15*REGBYTES(sp)
    LOAD x16, 16*REGBYTES(sp)
    LOAD x17, 17*REGBYTES(sp)
    LOAD x18, 18*REGBYTES(sp)
    LOAD x19, 19*REGBYTES(sp)
    LOAD x20, 20*REGBYTES(sp)
    LOAD x21, 21*REGBYTES(sp)
    LOAD x22, 22*REGBYTES(sp)
    LOAD x23, 23*REGBYTES(sp)
    LOAD x24, 24*REGBYTES(sp)
    LOAD x25, 25*REGBYTES(sp)
    LOAD x26, 26*REGBYTES(sp)
    LOAD x27, 27*REGBYTES(sp)
    LOAD x28, 28*REGBYTES(sp)
    LOAD x29, 29*REGBYTES(sp)
    LOAD x30, 30*REGBYTES(sp)
    LOAD x31, 31*REGBYTES(sp)
    /*
     * 2 号槽保存的是陷入前的 sp，加载后即完成 frame 的释放。
     * 仍以上述 S 为例：恢复期间基址为 S-288，最后 LOAD x2 从
     * (S-288)+2*8 读取保存的 S，令 sp=S；若提前执行，后续加载将错误地
     * 以 S 为基址读取 frame 之外的数据。因此保存和恢复时 sp 都要特殊处理。
     * 此后不能再用 sp 读取旧 frame，也无须额外加回 frame 的大小。
     * stval/scause 仅用于诊断本次 Trap，不是 sret 所需的返回状态。
     */
    LOAD x2, 2*REGBYTES(sp)
    .endm

    .globl __alltraps
    /* .align 2 表示按 2^2 字节对齐，使入口地址低两位为 0。 */
    .align 2
__alltraps:
    SAVE_ALL
    # lab3: 2414067
    /*
     * 保存完成后的 sp 就是 frame 地址；按 RISC-V ABI 放入首参 a0。
     * call 写入的是当前 ra，原 ra 已在 frame 内，不会丢失。
     * C 函数按 ABI 恢复自己的栈后返回，直接落入紧随其后的 __trapret。
     */
    mv a0, sp
    call trap

    .globl __trapret
__trapret:
    RESTORE_ALL
    /*
     * sret 将 PC 设为 sepc，按 SPP 返回原权限，并令 SIE←SPIE。
     * 本实验的 Trap 来自 S 态，因此这里仍可返回 S 态，不是固定返回 U 态。
     */
    sret
