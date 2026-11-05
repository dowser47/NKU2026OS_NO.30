#ifndef __KERN_TRAP_TRAP_H__
#define __KERN_TRAP_TRAP_H__

#include <defs.h>

struct pushregs {
    uintptr_t zero, ra, sp, gp, tp;
    uintptr_t t0, t1, t2, s0, s1;
    uintptr_t a0, a1, a2, a3, a4, a5, a6, a7;
    uintptr_t s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
    uintptr_t t3, t4, t5, t6;
};

struct trapframe {
    struct pushregs gpr;
    uintptr_t status;
    uintptr_t epc;
    uintptr_t badvaddr;
    uintptr_t cause;
};

void idt_init(void);
void trap(struct trapframe *tf);
void print_trapframe(struct trapframe *tf);
void interrupt_handler(struct trapframe *tf);
void exception_handler(struct trapframe *tf);

#endif
