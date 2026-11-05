/* Lab3 support reconstructed from the local lab2026-book; see README.md. */
#include <trap.h>
#include <riscv.h>
#include <stdio.h>
#include <clock.h>
#include <sbi.h>
#include <assert.h>

#define TICK_NUM 100
/* The offline book names this constant but supplies no value: use 10 lines. */
#define SHUTDOWN_AFTER 10

static void print_ticks(void) {
    cprintf("%d ticks\n", TICK_NUM);
#ifdef DEBUG_GRADE
    cprintf("End of Test.\n");
    panic("EOT: kernel seems ok.");
#endif
}

void idt_init(void) {
    extern void __alltraps(void);
    write_csr(sscratch, 0);
    write_csr(stvec, &__alltraps);
}

void print_trapframe(struct trapframe *tf) {
    const uintptr_t *regs = (const uintptr_t *)&tf->gpr;
    cprintf("trapframe at %p\n", tf);
    for (int i = 0; i < 32; ++i)
        cprintf("  x%d 0x%016lx\n", i, (unsigned long)regs[i]);
    cprintf("  status   0x%016lx\n", (unsigned long)tf->status);
    cprintf("  epc      0x%016lx\n", (unsigned long)tf->epc);
    cprintf("  badvaddr 0x%016lx\n", (unsigned long)tf->badvaddr);
    cprintf("  cause    0x%016lx\n", (unsigned long)tf->cause);
}

void interrupt_handler(struct trapframe *tf) {
    /* lab3: YOUR CODE (P2, intentionally pending) */
}

void exception_handler(struct trapframe *tf) {
    /* lab3: YOUR CODE (P3, intentionally pending) */
}

static inline void trap_dispatch(struct trapframe *tf) {
    if ((intptr_t)tf->cause < 0) {
        interrupt_handler(tf);
    } else {
        exception_handler(tf);
    }
}

void trap(struct trapframe *tf) { trap_dispatch(tf); }
